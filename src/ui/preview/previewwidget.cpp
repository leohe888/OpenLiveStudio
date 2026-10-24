#include "previewwidget.h"
#include "compositionsession.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLTextureBlitter>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

PreviewWidget::PreviewWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    auto requested = QSurfaceFormat::defaultFormat();
    requested.setRenderableType(QSurfaceFormat::OpenGL);
    requested.setVersion(3, 3);
    requested.setProfile(QSurfaceFormat::CoreProfile);
    requested.setSamples(0); // Blit from a single-sample composition texture.
    setFormat(requested);
    setMinimumSize(160, 90);
}

PreviewWidget::~PreviewWidget()
{
    QString error;
    if (!releaseGraphics(&error)) qFatal("PreviewWidget destruction: %s", qPrintable(error));
}

bool PreviewWidget::setSession(CompositionSession *session, QString *error)
{
    if (error) error->clear();
    if (session == m_session) return true;
    if (!returnFrames(error)) return false;
    disconnect(m_frameConnection);
    disconnect(m_errorConnection);
    m_session = session;
    if (session) {
        m_frameConnection = connect(session, &CompositionSession::frameAvailable, this,
                                    [this] { update(); });
        m_errorConnection = connect(session, &CompositionSession::errorOccurred,
                                    this, &PreviewWidget::reportError);
    }
    m_lastError.clear();
    update();
    return true;
}

QRectF PreviewWidget::canvasRect() const
{
    const QSize canvas = m_frame.token ? m_frame.size : QSize(1920, 1080);
    const QSizeF fitted = QSizeF(canvas).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - fitted.width()) / 2, (height() - fitted.height()) / 2), fitted);
}

void PreviewWidget::setSurroundingColor(const QColor &color)
{
    if (!color.isValid() || color == m_surroundingColor) return;
    m_surroundingColor = color;
    update();
}

void PreviewWidget::setCanvasColor(const QColor &color)
{
    if (!color.isValid() || color == m_canvasColor) return;
    m_canvasColor = color;
    update();
}

void PreviewWidget::reportError(const QString &message)
{
    if (message.isEmpty() || message == m_lastError) return;
    m_lastError = message;
    emit graphicsError(message);
}

void PreviewWidget::initializeGL()
{
    if (context()->isOpenGLES() || context()->format().version() < qMakePair(3, 3)) {
        reportError(tr("预览需要桌面 OpenGL 3.3 或更高版本。"));
        return;
    }
    auto blitter = std::make_unique<QOpenGLTextureBlitter>();
    if (!blitter->create()) {
        reportError(tr("无法创建预览纹理绘制资源。"));
        return;
    }
    m_blitter = std::move(blitter);
    m_gl = context()->extraFunctions();
    m_cleanupConnection = connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, [this] {
        QString error;
        if (!releaseGraphics(&error)) reportError(error);
    }, Qt::DirectConnection);
    emit contextReady(context());
    // Coordinator startup may have made another context current on this thread.
    makeCurrent();
}

bool PreviewWidget::returnFrames(QString *error)
{
    if (!m_pending.token && !m_frame.token) return true;
    if (!m_session || !isValid())
        return fail(error, tr("无法在有效的预览上下文中归还合成帧。"));
    const bool wasCurrent = QOpenGLContext::currentContext() == context();
    makeCurrent();
    if (QOpenGLContext::currentContext() != context())
        return fail(error, tr("无法激活预览上下文。"));
    bool ok = true;
    for (auto *frame : {&m_pending, &m_frame}) {
        if (frame->token) {
            if (!m_session->returnFrame(*frame, error)) { ok = false; break; }
            *frame = {};
        }
    }
    if (ok) m_displayedToken = 0;
    if (!wasCurrent) doneCurrent();
    return ok;
}

bool PreviewWidget::releaseGraphics(QString *error)
{
    if (error) error->clear();
    if (!returnFrames(error)) return false;
    if (!m_gl) return true;
    makeCurrent();
    if (QOpenGLContext::currentContext() != context())
        return fail(error, tr("无法激活预览上下文以清理资源。"));
    m_blitter->destroy();
    m_blitter.reset();
    m_gl = nullptr;
    disconnect(m_cleanupConnection);
    // Direct GUI-thread handler must stop the producer before this anchor dies.
    emit contextAboutToBeReleased();
    doneCurrent();
    return true;
}

void PreviewWidget::paintGL()
{
    if (!m_gl) return;
    m_displayedToken = 0;
    const qreal scale = devicePixelRatioF();
    const int pixelWidth = qRound(width() * scale), pixelHeight = qRound(height() * scale);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    m_gl->glDisable(GL_SCISSOR_TEST);
    m_gl->glViewport(0, 0, pixelWidth, pixelHeight);
    m_gl->glClearColor(m_surroundingColor.redF(), m_surroundingColor.greenF(), m_surroundingColor.blueF(), 1);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);

    if (m_session && m_session->isRunning()) {
        QString error;
        if (!m_pending.token) m_pending = m_session->acquireFrame(&error);
        if (!error.isEmpty()) { reportError(error); return; }
        if (m_pending.token) {
            // Keep both leases if a return fails, so the new token is not lost.
            if (m_frame.token && !m_session->returnFrame(m_frame, &error)) {
                reportError(error);
                return;
            }
            m_frame = m_pending;
            m_pending = {};
        }
    }
    const QRectF rect = canvasRect();
    const int left = qRound(rect.left() * scale), right = qRound(rect.right() * scale);
    const int bottom = pixelHeight - qRound(rect.bottom() * scale);
    const int top = pixelHeight - qRound(rect.top() * scale);
    m_gl->glEnable(GL_SCISSOR_TEST);
    m_gl->glScissor(left, bottom, right - left, top - bottom);
    m_gl->glClearColor(m_canvasColor.redF(), m_canvasColor.greenF(), m_canvasColor.blueF(), 1);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
    m_gl->glDisable(GL_SCISSOR_TEST);
    if (!m_frame.token) return;

    if (!m_frame.previewTexture) {
        reportError(tr("合成帧缺少透明预览纹理。"));
        return;
    }
    // Read only after acquire's GPU wait. The output texture stays untouched.
    m_gl->glDisable(GL_DEPTH_TEST);
    m_gl->glDisable(GL_CULL_FACE);
    m_gl->glActiveTexture(GL_TEXTURE0);
    m_gl->glBindTexture(GL_TEXTURE_2D, m_frame.previewTexture);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    m_gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_blitter->bind();
    m_gl->glEnable(GL_BLEND);
    m_gl->glBlendEquation(GL_FUNC_ADD);
    m_gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // Premultiplied RGBA.
    const auto transform = QOpenGLTextureBlitter::targetTransform(
        QRectF(left, pixelHeight - top, right - left, top - bottom), QRect(0, 0, pixelWidth, pixelHeight));
    m_blitter->blit(m_frame.previewTexture, transform, QOpenGLTextureBlitter::OriginBottomLeft);
    m_gl->glDisable(GL_BLEND);
    m_blitter->release();
    // No hidden consumer binding should retain a returned texture.
    m_gl->glBindTexture(GL_TEXTURE_2D, 0);
    m_displayedToken = m_frame.token;
    m_lastError.clear();
}
