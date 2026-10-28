#include "previewwidget.h"

#include <QDebug>
#include <algorithm>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
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

PreviewWidget::PreviewWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

PreviewWidget::~PreviewWidget()
{
    cleanup();
}

void PreviewWidget::setSources(const QList<PreviewSource> &sources)
{
    m_sources = sources;
    for (auto &source : m_sources) {
        if (source.transform.size.isEmpty())
            source.transform = SourceGeometry::initial(source.pixelSize());
    }
    // Release imports even when a minimized preview will not receive paintGL().
    const auto obsolete = [&](const auto &resource) {
        const auto source = std::find_if(m_sources.cbegin(), m_sources.cend(),
            [&](const PreviewSource &entry) { return entry.id == resource.first; });
        return source == m_sources.cend() || !source->hasFrame()
            || (source->frame && source->frame->generation != resource.second.generation);
    };
    if (m_initialized && context() && std::any_of(m_textures.cbegin(), m_textures.cend(), obsolete)) {
        auto *previousContext = QOpenGLContext::currentContext();
        auto *previousSurface = previousContext ? previousContext->surface() : nullptr;
        if (previousContext != context()) makeCurrent();
        for (auto it = m_textures.begin(); it != m_textures.end();) {
            if (obsolete(*it)) it = m_textures.erase(it);
            else ++it;
        }
        if (previousContext != context()) {
            doneCurrent();
            if (previousContext && previousSurface) previousContext->makeCurrent(previousSurface);
        }
    }
    if (m_gesture != Gesture::None) {
        const auto *source = selectedSource();
        if (!source || source->id != m_gestureSourceId || source->locked)
            endGesture();
    }
    update();
}

void PreviewWidget::setSelectedSourceId(const QString &id)
{
    if (m_selectedSourceId == id) return;
    endGesture();
    m_selectedSourceId = id;
    update();
}

void PreviewWidget::setSelectionColor(const QColor &color)
{
    if (!color.isValid() || m_selectionColor == color) return;
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
    m_contextDestruction = connect(context(), &QOpenGLContext::aboutToBeDestroyed,
                                  this, &PreviewWidget::cleanup, Qt::DirectConnection);
    m_lastError.clear();
    m_initialized = false;
    if (context()->isOpenGLES() || context()->format().version() < qMakePair(3, 3)) {
        reportError(tr("预览需要 OpenGL 3.3 或更高版本。"));
        return;
    }

    m_program = std::make_unique<QOpenGLShaderProgram>();
    if (!m_program->addShaderFromSourceFile(QOpenGLShader::Vertex,
                                           QStringLiteral(":/assets/shaders/texture.vert"))
        || !m_program->addShaderFromSourceFile(QOpenGLShader::Fragment,
                                              QStringLiteral(":/assets/shaders/texture.frag"))
        || !m_program->link() || !m_program->bind()) {
        reportError(tr("无法初始化预览着色器：%1").arg(m_program->log()));
        return;
    }

    // Position (x, y), then top-left-origin image coordinates (u, v).
    static constexpr GLfloat vertices[] = {
        -1, -1, 0, 1,
         1, -1, 1, 1,
        -1,  1, 0, 0,
         1,  1, 1, 0,
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
    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousTexture = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    QOpenGLFramebufferObjectFormat canvasFormat;
    canvasFormat.setTextureTarget(GL_TEXTURE_2D);
    canvasFormat.setInternalTextureFormat(GL_RGBA8);
    canvasFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    canvasFormat.setSamples(0);
    m_canvasFbo = std::make_unique<QOpenGLFramebufferObject>(QSize(1920, 1080), canvasFormat);
    const bool canvasValid = m_canvasFbo->isValid();
    if (!canvasValid)
        m_canvasFbo.reset();
    else {
        glBindTexture(GL_TEXTURE_2D, m_canvasFbo->texture());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, GLuint(previousTexture));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousReadFramebuffer));
    if (!canvasValid) {
        reportError(tr("无法创建 1920×1080 合成画布。"));
        return;
    }
    m_initialized = true;
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
    glDisable(GL_BLEND);
    glClearColor(m_surroundingColor.redF(), m_surroundingColor.greenF(),
                 m_surroundingColor.blueF(), 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!m_initialized || pixelSize.isEmpty())
        return;

    const auto restoreWidgetTarget = qScopeGuard([&] {
        glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
        glViewport(0, 0, pixelSize.width(), pixelSize.height());
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_STENCIL_TEST);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        m_vertexArray->release();
        m_program->release();
    });
    const QSize canvasSize = m_canvasFbo->size();
    if (!m_canvasFbo->bind()) {
        reportError(tr("无法绑定合成画布。"));
        return;
    }
    glViewport(0, 0, canvasSize.width(), canvasSize.height());
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    for (auto it = m_textures.begin(); it != m_textures.end();) {
        const bool retained = std::any_of(m_sources.cbegin(), m_sources.cend(),
            [&](const PreviewSource &source) { return source.id == it->first && source.hasFrame(); });
        if (!retained) it = m_textures.erase(it);
        else ++it;
    }
    for (const auto &source : m_sources) {
        if (!source.visible || !source.hasFrame()) continue;
        auto &resource = m_textures[source.id];
        if (source.frame ? (resource.generation == source.frame->generation && resource.sequence == source.frame->sequence)
                         : resource.imageKey == source.image.cacheKey()) continue;
        resource.ready = false;
        resource.imageKey = source.cpuImage().cacheKey();
        resource.generation = source.frame ? source.frame->generation : 0;
        resource.sequence = source.frame ? source.frame->sequence : 0;
        GLint maxTextureSize = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
        QString error;
        if (source.pixelSize().width() > maxTextureSize || source.pixelSize().height() > maxTextureSize) {
            error = tr("预览图片尺寸超过显卡纹理限制。");
        } else {
            if (!resource.texture || QSize(resource.texture->width(), resource.texture->height()) != source.pixelSize()) {
                resource.texture = std::make_unique<QOpenGLTexture>(QOpenGLTexture::Target2D);
                resource.texture->setFormat(QOpenGLTexture::RGBA8_UNorm);
                resource.texture->setSize(source.pixelSize().width(), source.pixelSize().height());
                resource.texture->allocateStorage(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8);
                resource.texture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
                resource.texture->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
            if (source.frame && source.frame->gpu) {
                if (resource.transferFailed) continue;
                if (!resource.interop) resource.interop = std::make_unique<GpuInterop>();
                QString reason;
                if (!resource.interop->copy(*source.frame, resource.texture->textureId(), &reason)) {
                    resource.transferFailed = true;
                    emit gpuTransferFailed(source.id, source.frame->generation, reason);
                    continue;
                }
            } else {
                resource.interop.reset();
                const auto image = source.cpuImage().convertToFormat(QImage::Format_RGBA8888);
                resource.texture->setData(QOpenGLTexture::RGBA, QOpenGLTexture::UInt8, image.constBits());
            }
            const GLenum uploadError = glGetError();
            if (!resource.texture->isCreated() || !resource.texture->isStorageAllocated()
                || uploadError != GL_NO_ERROR) {
                resource.texture.reset();
                error = tr("无法创建预览图片纹理。");
            } else {
                resource.ready = true;
            }
        }
        if (!error.isEmpty() && error != resource.uploadError) emit sourceError(source.id, error);
        resource.uploadError = error;
    }
    if (!m_program->bind()) {
        m_initialized = false;
        reportError(tr("无法绑定预览着色器：%1").arg(m_program->log()));
        return;
    }
    glActiveTexture(GL_TEXTURE0);
    m_vertexArray->bind();
    const auto drawTexture = [&](GLuint texture, const QMatrix4x4 &matrix, bool flipY) {
        m_program->setUniformValue("vertexTransform", matrix);
        m_program->setUniformValue("flipY", flipY);
        glBindTexture(GL_TEXTURE_2D, texture);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };
    QMatrix4x4 projection;
    projection.ortho(0.f, float(canvasSize.width()), float(canvasSize.height()), 0.f, -1.f, 1.f);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    for (auto it = m_sources.crbegin(); it != m_sources.crend(); ++it) {
        const auto resource = m_textures.find(it->id);
        if (drawable(*it)) {
            bool flipY = false;
            const GLuint texture = filterTexture(*it, resource->second.texture->textureId(), flipY);
            m_canvasFbo->bind();
            glViewport(0, 0, canvasSize.width(), canvasSize.height());
            glEnable(GL_BLEND);
            m_program->setUniformValue("colorCorrection", false);
            QMatrix4x4 model(SourceGeometry::sourceToCanvas(it->transform));
            model.translate(float(it->transform.size.width() / 2), float(it->transform.size.height() / 2));
            model.scale(float(it->transform.size.width() / 2), float(-it->transform.size.height() / 2));
            drawTexture(texture, projection * model, flipY);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, pixelSize.width(), pixelSize.height());
    glDisable(GL_BLEND);
    m_program->setUniformValue("colorCorrection", false);
    // FBO textures use bottom-left-origin coordinates, unlike uploaded images.
    const qreal scale = qMin(qreal(pixelSize.width()) / canvasSize.width(),
                            qreal(pixelSize.height()) / canvasSize.height());
    QMatrix4x4 display;
    display.scale(float(canvasSize.width() * scale / pixelSize.width()),
                  float(canvasSize.height() * scale / pixelSize.height()));
    drawTexture(m_canvasFbo->texture(), display, true);
    m_vertexArray->release();
    m_program->release();
    paintSelection();
}

GLuint PreviewWidget::filterTexture(const PreviewSource &source, GLuint original, bool &flipY)
{
    flipY = false;
    auto &error = m_textures.at(source.id).filterError;
    const auto fail = [&](const QString &message) {
        if (error != message) {
            error = message;
            emit sourceError(source.id, message);
        }
        flipY = false;
        return original;
    };
    GLuint input = original;
    int pass = 0;
    for (const auto &filter : source.filters) {
        if (!filter.enabled) continue;
        if (!filter.valid()) return fail(tr("色彩校正参数无效，已使用原图。"));
        if (filter.isIdentity()) continue;
        auto &target = m_filterFbos[pass % 2];
        if (!target || target->size() != source.pixelSize()) {
            QOpenGLFramebufferObjectFormat format;
            format.setInternalTextureFormat(GL_RGBA8);
            format.setAttachment(QOpenGLFramebufferObject::NoAttachment);
            target = std::make_unique<QOpenGLFramebufferObject>(source.pixelSize(), format);
            if (!target->isValid()) {
                target.reset();
                return fail(tr("无法创建滤镜缓冲，已使用原图。"));
            }
            glBindTexture(GL_TEXTURE_2D, target->texture());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        if (!target->bind()) return fail(tr("无法绑定滤镜缓冲，已使用原图。"));
        glViewport(0, 0, source.pixelSize().width(), source.pixelSize().height());
        glDisable(GL_BLEND);
        m_program->setUniformValue("vertexTransform", QMatrix4x4());
        m_program->setUniformValue("flipY", pass > 0);
        m_program->setUniformValue("colorCorrection", true);
        m_program->setUniformValue("correction", filter.brightness, filter.contrast, filter.saturation);
        glBindTexture(GL_TEXTURE_2D, input);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        if (glGetError() != GL_NO_ERROR) return fail(tr("滤镜处理失败，已使用原图。"));
        input = target->texture();
        ++pass;
    }
    error.clear();
    flipY = pass > 0;
    return input;
}

bool PreviewWidget::drawable(const PreviewSource &source) const
{
    const auto texture = m_textures.find(source.id);
    return source.visible && source.hasFrame() && SourceGeometry::valid(source.transform)
        && texture != m_textures.end() && texture->second.texture && texture->second.ready
        && (source.frame ? (texture->second.generation == source.frame->generation && texture->second.sequence == source.frame->sequence)
                         : texture->second.imageKey == source.image.cacheKey());
}

const PreviewSource *PreviewWidget::selectedSource() const
{
    for (const auto &source : m_sources)
        if (source.id == m_selectedSourceId && drawable(source)) return &source;
    return nullptr;
}

QPointF PreviewWidget::toCanvas(const QPointF &point) const
{
    return SourceGeometry::canvasToWidget(size()).inverted().map(point);
}

QPolygonF PreviewWidget::selectionHandles(const PreviewSource &source) const
{
    const auto local = SourceGeometry::sourceToCanvas(source.transform);
    const auto display = SourceGeometry::canvasToWidget(size());
    QPolygonF handles;
    for (const auto &fraction : SourceGeometry::handleFractions)
        handles.append(display.map(local.map(QPointF(fraction.x() * source.transform.size.width(),
                                                     fraction.y() * source.transform.size.height()))));
    const QPointF outward = handles[1] - display.map(source.transform.center);
    const qreal length = std::hypot(outward.x(), outward.y());
    handles.append(handles[1] + outward * (24 / length));
    return handles;
}

int PreviewWidget::handleAt(const PreviewSource &source, const QPointF &point) const
{
    if (source.locked) return -1;
    const auto handles = selectionHandles(source);
    // Prefer rotation and corners if small sources make handle targets overlap.
    for (int index : {8, 0, 2, 4, 6, 1, 3, 5, 7})
        if (QRectF(handles[index] - QPointF(6, 6), QSizeF(12, 12)).contains(point)) return index;
    return -1;
}

void PreviewWidget::paintSelection()
{
    const auto *source = selectedSource();
    if (!source) return;
    const auto handles = selectionHandles(*source);
    const QPolygonF outline{handles[0], handles[2], handles[4], handles[6]};
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const auto style = source->locked ? Qt::DashLine : Qt::SolidLine;
    painter.setPen(QPen(QColor(0, 0, 0, 220), 3, style));
    painter.drawPolygon(outline);
    painter.setPen(QPen(m_selectionColor, 1, style));
    painter.drawPolygon(outline);
    if (source->locked) return;
    painter.setPen(QPen(Qt::black, 3));
    painter.drawLine(handles[1], handles[8]);
    painter.setPen(QPen(m_selectionColor, 1));
    painter.drawLine(handles[1], handles[8]);
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(m_selectionColor);
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
        if (handle == 8) { setCursor(Qt::OpenHandCursor); return; }
        if (handle >= 0) {
            const auto fraction = SourceGeometry::handleFractions[handle];
            const qreal angle = source->transform.rotation
                + std::atan2(fraction.y() - .5, fraction.x() - .5) * 180 / std::acos(-1.0);
            const int direction = (qRound(angle / 45) % 4 + 4) % 4;
            static constexpr Qt::CursorShape cursors[]{Qt::SizeHorCursor, Qt::SizeFDiagCursor,
                                                       Qt::SizeVerCursor, Qt::SizeBDiagCursor};
            setCursor(cursors[direction]);
            return;
        }
    }
    if (SourceGeometry::canvasRect(size()).contains(position)) {
        for (const auto &source : m_sources) {
            if (drawable(source) && !source.locked && SourceGeometry::contains(source.transform, toCanvas(position))) {
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
        if (SourceGeometry::canvasRect(size()).contains(position)) {
            for (const auto &candidate : m_sources) {
                if (drawable(candidate) && !candidate.locked
                    && SourceGeometry::contains(candidate.transform, toCanvas(position))) {
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
    m_gestureStart = source->transform;
    m_pressCanvas = toCanvas(position);
    if (m_gesture == Gesture::Rotate) setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void PreviewWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_gesture == Gesture::None) {
        updateCursor(event->position());
        event->accept();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton)) endGesture();
    else updateGesture(event->position());
    event->accept();
}

void PreviewWidget::updateGesture(const QPointF &position)
{
    const auto *source = selectedSource();
    if (m_gesture == Gesture::None || !source || source->locked || source->id != m_gestureSourceId) {
        endGesture();
        return;
    }
    const QPointF point = toCanvas(position);
    SourceTransform result = m_gestureStart;
    switch (m_gesture) {
    case Gesture::Move:
        result.center += point - m_pressCanvas;
        break;
    case Gesture::Resize:
        result = SourceGeometry::resized(m_gestureStart, m_resizeHandle, point - m_pressCanvas);
        break;
    case Gesture::Rotate: {
        const QPointF before = m_pressCanvas - result.center;
        const QPointF after = point - result.center;
        if (std::hypot(after.x(), after.y()) < 1e-9) break;
        const qreal angle = (std::atan2(after.y(), after.x()) - std::atan2(before.y(), before.x()))
            * 180 / std::acos(-1.0);
        result.rotation = SourceGeometry::normalizedAngle(result.rotation + angle);
        break;
    }
    case Gesture::None: break;
    }
    const QString id = m_gestureSourceId;
    emit transformRequested(id, result);
}

void PreviewWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (m_gesture != Gesture::None) updateGesture(event->position());
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
        emit transformRequested(id, start);
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
    ) endGesture();
    return QOpenGLWidget::event(event);
}

void PreviewWidget::cleanup()
{
    endGesture();
    disconnect(m_contextDestruction);
    m_initialized = false;
    if (!context())
        return;

    makeCurrent();
    m_canvasFbo.reset();
    for (auto &fbo : m_filterFbos) fbo.reset();
    m_textures.clear();
    m_vertexArray.reset();
    m_vertexBuffer.reset();
    m_program.reset();
    doneCurrent();
    // Retain CPU images so a replacement context can upload them again.
}

void PreviewWidget::reportError(const QString &message)
{
    m_lastError = message;
    qWarning().noquote() << message;
    emit renderingError(message);
}
