#include "imagecompositor.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QThread>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
}

ImageCompositor::ImageCompositor() = default;
ImageCompositor::~ImageCompositor() { release(); }

bool ImageCompositor::initialize(QOffscreenSurface *surface, QString *error)
{
    if (error) error->clear();
    if (m_context) return fail(error, QStringLiteral("Compositor is already initialized."));
    if (!surface || !surface->isValid())
        return fail(error, QStringLiteral("A valid GUI-created offscreen surface is required."));
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(surface->format());
    if (!context->create() || !context->makeCurrent(surface))
        return fail(error, QStringLiteral("Cannot create or activate the offscreen OpenGL context."));
    context->doneCurrent();
    m_surface = surface;
    m_context = std::move(context);
    return true;
}

bool ImageCompositor::makeCurrent(QString *error)
{
    if (!m_context) return fail(error, QStringLiteral("Compositor is not initialized."));
    if (m_context->thread() != QThread::currentThread())
        return fail(error, QStringLiteral("Compositor called from the wrong thread."));
    if (!m_context->makeCurrent(m_surface))
        return fail(error, QStringLiteral("Cannot activate the offscreen OpenGL context."));
    return true;
}

bool ImageCompositor::render(const QList<Layer> &layers, const QSize &canvas, QString *error)
{
    if (error) error->clear();
    m_hasFrame = false;
    if (canvas.isEmpty()) return fail(error, QStringLiteral("Canvas dimensions must be positive."));
    for (const auto &layer : layers) {
        if (layer.visible && layer.image.isNull())
            return fail(error, QStringLiteral("A visible layer has no decoded image."));
    }
    if (!makeCurrent(error)) return false;
    auto *gl = m_context->functions();
    GLint limit = 0;
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (canvas.width() > limit || canvas.height() > limit) {
        m_context->doneCurrent();
        return fail(error, QStringLiteral("Canvas exceeds the OpenGL texture size limit."));
    }
    if (!m_framebuffer || m_framebuffer->size() != canvas) {
        m_framebuffer.reset();
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        m_framebuffer = std::make_unique<QOpenGLFramebufferObject>(canvas, format);
    }
    if (!m_framebuffer->isValid() || !m_framebuffer->bind()) {
        m_context->doneCurrent();
        return fail(error, QStringLiteral("Cannot create or bind the composition framebuffer."));
    }
    gl->glViewport(0, 0, canvas.width(), canvas.height());
    QOpenGLPaintDevice device(canvas);
    QPainter painter(&device);
    if (!painter.isActive()) {
        m_framebuffer->release();
        m_context->doneCurrent();
        return fail(error, QStringLiteral("Cannot start the OpenGL paint engine."));
    }
    painter.fillRect(QRect(QPoint(), canvas), Qt::black);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
        if (!it->visible) continue;
        const QSizeF fitted = QSizeF(it->image.size()).scaled(QSizeF(canvas), Qt::KeepAspectRatio);
        const QPointF origin((canvas.width() - fitted.width()) / 2,
                             (canvas.height() - fitted.height()) / 2);
        painter.drawImage(QRectF(origin, fitted), it->image);
    }
    painter.end();
    m_framebuffer->release();
    m_context->doneCurrent();
    m_hasFrame = true;
    return true;
}

unsigned int ImageCompositor::texture() const
{
    return m_hasFrame && m_framebuffer ? m_framebuffer->texture() : 0;
}

QImage ImageCompositor::readback(QString *error)
{
    if (error) error->clear();
    if (!m_hasFrame) {
        fail(error, QStringLiteral("No completed composition frame."));
        return {};
    }
    if (!makeCurrent(error)) return {};
    const QImage image = m_framebuffer->toImage();
    m_context->doneCurrent();
    if (image.isNull()) fail(error, QStringLiteral("Cannot read the composition framebuffer."));
    return image;
}

void ImageCompositor::release()
{
    if (!m_context) return;
    Q_ASSERT(m_context->thread() == QThread::currentThread());
    m_context->makeCurrent(m_surface);
    m_framebuffer.reset();
    m_context->doneCurrent();
    m_context.reset();
    m_surface = nullptr;
    m_hasFrame = false;
}
