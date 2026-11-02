#include "ui/previewwidget.h"

#include <QDebug>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QScopeGuard>
#include <QShowEvent>
#include <QSurfaceFormat>
#include <QMatrix4x4>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QFocusEvent>
#include <QHideEvent>

#include <array>
#include <cmath>

namespace {
// Clockwise from the upper-left corner: corners alternate with edge midpoints.
const std::array<QPointF, 8> handleFractions{
    {{0, 0}, {.5, 0}, {1, 0}, {1, .5}, {1, 1}, {.5, 1}, {0, 1}, {0, .5}}};

}

QRectF PreviewWidget::canvasRect() const
{
    const QSizeF widgetSize = size();
    if (widgetSize.isEmpty())
        return {};
    const qreal scale = qMin(widgetSize.width() / 1920, widgetSize.height() / 1080);
    const QSizeF size(1920 * scale, 1080 * scale);
    return QRectF(
        QPointF((widgetSize.width() - size.width()) / 2, (widgetSize.height() - size.height()) / 2),
        size);
}

QTransform PreviewWidget::canvasToWidget() const
{
    const QRectF rect = canvasRect();
    QTransform matrix;
    matrix.translate(rect.x(), rect.y());
    matrix.scale(rect.width() / 1920, rect.height() / 1080);
    return matrix;
}

VideoSourceCanvasGeometry PreviewWidget::resized(const VideoSourceCanvasGeometry &start, int handle,
                                            const QPointF &canvasDelta)
{
    if (!start.isValid() || handle < 0 || handle >= 8)
        return start;
    QTransform rotation;
    rotation.rotate(start.rotation());
    const QPointF delta = rotation.inverted().map(canvasDelta);
    const QPointF fraction = handleFractions[handle];
    const QPointF opposite(1 - fraction.x(), 1 - fraction.y());
    VideoSourceCanvasGeometry result = start;
    const qreal w = start.size().width(), h = start.size().height();
    if (handle % 2 == 0) {
        const QPointF diagonal((2 * fraction.x() - 1) * w, (2 * fraction.y() - 1) * h);
        const qreal factor =
            qMax(qMax(1 / w, 1 / h), QPointF::dotProduct(diagonal + delta, diagonal) /
                                         QPointF::dotProduct(diagonal, diagonal));
        result.setSize(start.size() * factor);
    } else if (fraction.x() != .5) {
        result.setSize(QSizeF(qMax(1.0, w + (2 * fraction.x() - 1) * delta.x()), h));
    } else {
        result.setSize(QSizeF(w, qMax(1.0, h + (2 * fraction.y() - 1) * delta.y())));
    }
    const QPointF anchor = start.sourceToCanvas().map(QPointF(opposite.x() * w, opposite.y() * h));
    result.setCenter(anchor + rotation.map(QPointF((.5 - opposite.x()) * result.size().width(),
                                                  (.5 - opposite.y()) * result.size().height())));
    return result;
}

PreviewWidget::PreviewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    connect(&m_compositor, &Compositor::sourceFailed, this, &PreviewWidget::sourceFailed,
            Qt::QueuedConnection);
    connect(&m_compositor, &Compositor::renderingFailed, this, &PreviewWidget::reportError,
            Qt::QueuedConnection);
    connect(&m_compositor, &Compositor::gpuTransferFailed, this, &PreviewWidget::gpuTransferFailed,
            Qt::QueuedConnection);
    m_framePoll.setInterval(16);
    m_framePoll.setTimerType(Qt::PreciseTimer);
    connect(&m_framePoll, &QTimer::timeout, this, [this] {
        const auto frame = m_compositor.latestFrame();
        if (frame && (!m_presentedFrame || frame->sequence != m_presentedFrame->sequence))
            update();
    });
}

PreviewWidget::~PreviewWidget()
{
    cleanup();
}

void PreviewWidget::setSources(const QList<PreviewSource> &sources)
{
    m_sources = sources;
    for (auto &source : m_sources) {
        if (source.canvasGeometry.size().isEmpty())
            source.canvasGeometry = VideoSourceCanvasGeometry(source.pixelSize());
    }
    m_compositor.setSources(m_sources);
    if (m_gesture != Gesture::None) {
        const auto *source = selectedSource();
        if (!source || source->id != m_gestureSourceId || source->locked)
            endGesture();
    }
    update();
}

void PreviewWidget::setCurrentSourceId(const QString &sourceId)
{
    if (m_currentSourceId == sourceId)
        return;
    endGesture();
    m_currentSourceId = sourceId;
    update();
}

void PreviewWidget::setSelectionColor(const QColor &color)
{
    if (!color.isValid() || m_selectionColor == color)
        return;
    m_selectionColor = color;
    update();
}

void PreviewWidget::setSurroundingColor(const QColor &color)
{
    if (!color.isValid() || m_surroundingColor == color)
        return;
    m_surroundingColor = color;
    update();
}

void PreviewWidget::showEvent(QShowEvent *event)
{
    QOpenGLWidget::showEvent(event);
    if (!isValid() && m_lastError.isEmpty())
        reportError(tr("无法创建 OpenGL 预览上下文。"));
}

void PreviewWidget::initializeGL()
{
    initializeOpenGLFunctions();
    m_contextDestruction = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
                                   &PreviewWidget::cleanup, Qt::DirectConnection);
    m_lastError.clear();
    m_initialized = false;
    if (context()->isOpenGLES() || context()->format().version() < qMakePair(3, 3)) {
        reportError(tr("预览需要 OpenGL 3.3 或更高版本。"));
        return;
    }

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceFile(QOpenGLShader::Vertex,
                                            QStringLiteral(":/assets/shaders/texture.vert")) ||
        !m_program->addShaderFromSourceFile(QOpenGLShader::Fragment,
                                            QStringLiteral(":/assets/shaders/texture.frag")) ||
        !m_program->link() || !m_program->bind()) {
        reportError(tr("无法初始化预览着色器：%1").arg(m_program->log()));
        return;
    }

    // Position (x, y), then top-left-origin image coordinates (u, v).
    static constexpr GLfloat vertices[] = {
        -1, -1, 0, 1, 1, -1, 1, 1, -1, 1, 0, 0, 1, 1, 1, 0,
    };
    m_vertexArray = std::make_unique<QOpenGLVertexArrayObject>();
    m_vertexBuffer = std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer);
    if (!m_vertexArray->create() || !m_vertexBuffer->create()) {
        m_program->release();
        reportError(tr("无法创建预览顶点资源。"));
        return;
    }
    m_vertexArray->bind();
    if (!m_vertexBuffer->bind()) {
        m_vertexArray->release();
        m_program->release();
        reportError(tr("无法绑定预览顶点缓冲。"));
        return;
    }
    m_vertexBuffer->setUsagePattern(QOpenGLBuffer::StaticDraw);
    m_vertexBuffer->allocate(vertices, sizeof(vertices));
    const bool bufferAllocated = m_vertexBuffer->size() == sizeof(vertices);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(GLfloat));
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(GLfloat), 2, 4 * sizeof(GLfloat));
    m_program->setUniformValue("sourceTexture", 0);
    m_vertexArray->release();
    m_vertexBuffer->release();
    m_program->release();
    if (!bufferAllocated) {
        reportError(tr("无法分配预览顶点缓冲。"));
        return;
    }
    m_initialized = m_compositor.startRendering(context());
    if (m_initialized)
        m_framePoll.start();
}

void PreviewWidget::paintGL()
{
    const QSize pixelSize(qRound(width() * devicePixelRatioF()),
                          qRound(height() * devicePixelRatioF()));
    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glClearColor(m_surroundingColor.redF(), m_surroundingColor.greenF(), m_surroundingColor.blueF(),
                 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_initialized || pixelSize.isEmpty())
        return;

    // The previous paint completed its reads before this lease can be released.
    m_presentedFrame = m_compositor.latestFrame();
    if (!m_presentedFrame)
        return;
    const auto restore = qScopeGuard([&] {
        glBindTexture(GL_TEXTURE_2D, 0);
        m_vertexArray->release();
        m_program->release();
    });
    if (!m_program->bind()) {
        reportError(tr("无法绑定预览着色器：%1").arg(m_program->log()));
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    m_vertexArray->bind();
    const QSize canvasSize = m_presentedFrame->size;
    const qreal scale = qMin(qreal(pixelSize.width()) / canvasSize.width(),
                             qreal(pixelSize.height()) / canvasSize.height());
    QMatrix4x4 display;
    display.scale(float(canvasSize.width() * scale / pixelSize.width()),
                  float(canvasSize.height() * scale / pixelSize.height()));
    m_program->setUniformValue("vertexTransform", display);
    m_program->setUniformValue("flipY", true);
    m_program->setUniformValue("colorCorrection", false);
    glBindTexture(GL_TEXTURE_2D, m_presentedFrame->texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glFinish(); // Output slot may be reused by the producer after the lease ends.
    m_vertexArray->release();
    m_program->release();
    paintSelection();
}

bool PreviewWidget::selectable(const PreviewSource &source) const
{
    if (!m_presentedFrame || !source.visible || !source.hasFrame() ||
        !source.canvasGeometry.isValid())
        return false;
    const auto resource = m_presentedFrame->sources.constFind(source.id);
    return resource != m_presentedFrame->sources.cend() &&
           resource->generation == (source.frame ? source.frame->generation : 0);
}

const PreviewSource *PreviewWidget::selectedSource() const
{
    for (const auto &source : m_sources)
        if (source.id == m_currentSourceId && selectable(source))
            return &source;
    return nullptr;
}

QPointF PreviewWidget::toCanvas(const QPointF &point) const
{
    return canvasToWidget().inverted().map(point);
}

QPolygonF PreviewWidget::selectionHandles(const PreviewSource &source) const
{
    const auto local = source.canvasGeometry.sourceToCanvas();
    const auto display = canvasToWidget();
    QPolygonF handles;
    for (const auto &fraction : handleFractions)
        handles.append(
            display.map(local.map(QPointF(fraction.x() * source.canvasGeometry.size().width(),
                                          fraction.y() * source.canvasGeometry.size().height()))));
    const QPointF outward = handles[1] - display.map(source.canvasGeometry.center());
    const qreal length = std::hypot(outward.x(), outward.y());
    handles.append(handles[1] + outward * (24 / length));
    return handles;
}

int PreviewWidget::handleAt(const PreviewSource &source, const QPointF &point) const
{
    if (source.locked)
        return -1;
    const auto handles = selectionHandles(source);
    // Prefer rotation and corners if small sources make handle targets overlap.
    for (int index : {8, 0, 2, 4, 6, 1, 3, 5, 7})
        if (QRectF(handles[index] - QPointF(6, 6), QSizeF(12, 12)).contains(point))
            return index;
    return -1;
}

void PreviewWidget::paintSelection()
{
    const auto *source = selectedSource();
    if (!source)
        return;
    const auto handles = selectionHandles(*source);
    const QPolygonF outline{handles[0], handles[2], handles[4], handles[6]};
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const auto style = source->locked ? Qt::DashLine : Qt::SolidLine;
    painter.setPen(QPen(QColor(0, 0, 0, 220), 3, style));
    painter.drawPolygon(outline);
    painter.setPen(QPen(selectionColor(), 1, style));
    painter.drawPolygon(outline);
    if (source->locked)
        return;
    painter.setPen(QPen(Qt::black, 3));
    painter.drawLine(handles[1], handles[8]);
    painter.setPen(QPen(selectionColor(), 1));
    painter.drawLine(handles[1], handles[8]);
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(selectionColor());
    for (int index = 0; index < 8; ++index)
        painter.drawRect(QRectF(handles[index] - QPointF(4, 4), QSizeF(8, 8)));
    painter.drawEllipse(QRectF(handles[8] - QPointF(4, 4), QSizeF(8, 8)));
}

void PreviewWidget::endGesture()
{
    m_gesture = Gesture::None;
    m_gestureSourceId.clear();
    m_resizeHandle = -1;
    unsetCursor();
}

void PreviewWidget::updateCursor(const QPointF &position)
{
    if (const auto *source = selectedSource()) {
        const int handle = handleAt(*source, position);
        if (handle == 8) {
            setCursor(Qt::OpenHandCursor);
            return;
        }
        if (handle >= 0) {
            const auto fraction = handleFractions[handle];
            const qreal angle =
                source->canvasGeometry.rotation() +
                std::atan2(fraction.y() - .5, fraction.x() - .5) * 180 / std::acos(-1.0);
            const int direction = (qRound(angle / 45) % 4 + 4) % 4;
            static constexpr Qt::CursorShape cursors[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                                       Qt::SizeVerCursor, Qt::SizeBDiagCursor};
            setCursor(cursors[direction]);
            return;
        }
    }
    if (canvasRect().contains(position)) {
        for (const auto &source : m_sources) {
            if (selectable(source) && !source.locked &&
                source.canvasGeometry.contains(toCanvas(position))) {
                setCursor(Qt::SizeAllCursor);
                return;
            }
        }
    }
    unsetCursor();
}

void PreviewWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_initialized) {
        QOpenGLWidget::mousePressEvent(event);
        return;
    }
    endGesture();
    setFocus(Qt::MouseFocusReason);
    const QPointF position = event->position();
    const auto *source = selectedSource();
    const int handle = source ? handleAt(*source, position) : -1;
    if (handle >= 0) {
        m_gesture = handle == 8 ? Gesture::Rotate : Gesture::Resize;
        m_resizeHandle = handle;
    } else {
        QString hit;
        if (canvasRect().contains(position)) {
            for (const auto &candidate : m_sources) {
                if (selectable(candidate) && !candidate.locked &&
                    candidate.canvasGeometry.contains(toCanvas(position))) {
                    hit = candidate.id;
                    break;
                }
            }
        }
        emit selectionRequested(hit);
        source = selectedSource();
        if (hit.isEmpty() || !source || source->id != hit || source->locked) {
            event->accept();
            return;
        }
        m_gesture = Gesture::Move;
    }
    m_gestureSourceId = source->id;
    m_gestureStart = source->canvasGeometry;
    m_pressCanvas = toCanvas(position);
    if (m_gesture == Gesture::Rotate)
        setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gesture == Gesture::None) {
        updateCursor(event->position());
        event->accept();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton))
        endGesture();
    else
        updateGesture(event->position());
    event->accept();
}

void PreviewWidget::updateGesture(const QPointF &position)
{
    const auto *source = selectedSource();
    if (m_gesture == Gesture::None || !source || source->locked ||
        source->id != m_gestureSourceId) {
        endGesture();
        return;
    }
    const QPointF point = toCanvas(position);
    VideoSourceCanvasGeometry result = m_gestureStart;
    switch (m_gesture) {
    case Gesture::Move:
        result.setCenter(result.center() + point - m_pressCanvas);
        break;
    case Gesture::Resize:
        result = resized(m_gestureStart, m_resizeHandle, point - m_pressCanvas);
        break;
    case Gesture::Rotate: {
        const QPointF before = m_pressCanvas - result.center();
        const QPointF after = point - result.center();
        if (std::hypot(after.x(), after.y()) < 1e-9)
            break;
        const qreal angle =
            (std::atan2(after.y(), after.x()) - std::atan2(before.y(), before.x())) * 180 /
            std::acos(-1.0);
        result.setRotation(result.rotation() + angle);
        break;
    }
    case Gesture::None:
        break;
    }
    const QString id = m_gestureSourceId;
    emit canvasGeometryRequested(id, result);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_gesture != Gesture::None)
            updateGesture(event->position());
        endGesture();
        updateCursor(event->position());
        event->accept();
    } else {
        QOpenGLWidget::mouseReleaseEvent(event);
    }
}

void PreviewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_gesture != Gesture::None) {
        const QString id = m_gestureSourceId;
        const auto start = m_gestureStart;
        endGesture();
        emit canvasGeometryRequested(id, start);
        event->accept();
    } else {
        QOpenGLWidget::keyPressEvent(event);
    }
}

void PreviewWidget::resizeEvent(QResizeEvent *event)
{
    endGesture();
    QOpenGLWidget::resizeEvent(event);
}

void PreviewWidget::focusOutEvent(QFocusEvent *event)
{
    endGesture();
    QOpenGLWidget::focusOutEvent(event);
}

void PreviewWidget::hideEvent(QHideEvent *event)
{
    endGesture();
    QOpenGLWidget::hideEvent(event);
}

bool PreviewWidget::event(QEvent *event)
{
    if (event->type() == QEvent::WindowDeactivate
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        || event->type() == QEvent::DevicePixelRatioChange
#endif
    )
        endGesture();
    return QOpenGLWidget::event(event);
}

void PreviewWidget::cleanup()
{
    endGesture();
    disconnect(m_contextDestruction);
    m_framePoll.stop();
    m_initialized = false;
    // Finish preview reads and return its lease before destroying shared outputs.
    if (context()) {
        makeCurrent();
        glFinish();
    }
    m_presentedFrame.reset();
    m_compositor.stopRendering();
    m_vertexArray.reset();
    m_vertexBuffer.reset();
    m_program.reset();
    if (context())
        doneCurrent();
    // Retain the source snapshot for context recreation.
}

void PreviewWidget::reportError(const QString &message)
{
    m_lastError = message;
    qWarning().noquote() << message;
    emit renderingFailed(message);
}
