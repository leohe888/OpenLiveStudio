#include "previewwidget.h"
#include "compositionsession.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

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
    m_gl = context()->extraFunctions();
    m_gl->glGenFramebuffers(1, &m_readFramebuffer);
    if (!m_readFramebuffer) {
        m_gl = nullptr;
        reportError(tr("无法创建预览帧缓冲。"));
        return;
    }
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
    m_gl->glDeleteFramebuffers(1, &m_readFramebuffer);
    m_readFramebuffer = 0;
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
    m_gl->glClearColor(0, 0, 0, 1);
    m_gl->glClear(GL_COLOR_BUFFER_BIT);
    m_gl->glDisable(GL_SCISSOR_TEST);
    if (!m_frame.token) return;

    // Consumer-local FBO; shared texture is rebound AFTER acquire's GPU wait.
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFramebuffer);
    m_gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                GL_TEXTURE_2D, m_frame.texture, 0);
    const bool complete = m_gl->glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, defaultFramebufferObject());
        m_gl->glBlitFramebuffer(0, 0, m_frame.size.width(), m_frame.size.height(),
                               left, bottom, right, top, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    // Detach: no hidden consumer binding should retain a returned texture.
    m_gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    if (!complete) {
        m_displayedToken = 0;
        reportError(tr("无法读取共享合成纹理。"));
        return;
    }
    m_displayedToken = m_frame.token;
    m_lastError.clear();
}
