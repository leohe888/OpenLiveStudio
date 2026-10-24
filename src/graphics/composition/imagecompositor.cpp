#include "imagecompositor.h"

#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QPainter>
#include <QThread>
#include <array>
#include <atomic>

namespace {
bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}
bool supportsSync(QOpenGLContext *context)
{
    const auto format = context->format();
    return context->isOpenGLES() ? format.majorVersion() >= 3
        : format.version() >= qMakePair(3, 2) || context->hasExtension("GL_ARB_sync");
}

// Tokens remain distinct across compositor instances and restart cycles.
std::atomic<quint64> nextToken{1};
}

class ImageCompositor::Private
{
public:
    enum State { Idle, Writing, Ready, Acquired, Returning };
    struct Slot {
        std::unique_ptr<QOpenGLFramebufferObject> fbo;
        GLsync fence = nullptr;
        State state = Idle;
        quint64 token = 0;
        QOpenGLContext *consumer = nullptr;
    };
    QMutex mutex;
    std::array<Slot, BufferCount> buffers;
    QOffscreenSurface *surface = nullptr;
    std::unique_ptr<QOpenGLContext> context;
    QOpenGLContextGroup *group = nullptr;
    bool shared = false;
    int ready = -1;
    int last = -1;

    bool consumerCurrent(QString *error) const
    {
        auto *current = QOpenGLContext::currentContext();
        if (!context || !shared)
            return fail(error, QStringLiteral("Compositor is not initialized for sharing."));
        if (!current || current->shareGroup() != group || !supportsSync(current))
            return fail(error, QStringLiteral("A compatible sharing context must be current."));
        return true;
    }
};

ImageCompositor::ImageCompositor() : d(std::make_unique<Private>()) {}
ImageCompositor::~ImageCompositor()
{
    QString error;
    if (!release(&error)) qFatal("ImageCompositor destruction: %s", qPrintable(error));
}

bool ImageCompositor::initialize(QOffscreenSurface *surface, QString *error,
                                 QOpenGLContext *shareContext)
{
    if (error) error->clear();
    QMutexLocker lock(&d->mutex);
    if (d->context) return fail(error, QStringLiteral("Compositor is already initialized."));
    if (!surface || !surface->isValid())
        return fail(error, QStringLiteral("A valid GUI-created offscreen surface is required."));
    if (shareContext && !shareContext->isValid())
        return fail(error, QStringLiteral("The sharing context is invalid."));
    auto context = std::make_unique<QOpenGLContext>();
    context->setFormat(surface->format());
    context->setShareContext(shareContext);
    if (!context->create() || !context->makeCurrent(surface))
        return fail(error, QStringLiteral("Cannot create or activate the offscreen OpenGL context."));
    if (shareContext && (!QOpenGLContext::areSharing(context.get(), shareContext)
                         || !supportsSync(context.get()))) {
        context->doneCurrent();
        return fail(error, QStringLiteral("OpenGL context sharing or GPU sync is unavailable."));
    }
    context->doneCurrent();
    d->surface = surface;
    d->group = context->shareGroup();
    d->shared = shareContext != nullptr;
    d->context = std::move(context);
    return true;
}

bool ImageCompositor::makeCurrent(QString *error)
{
    if (!d->context) return fail(error, QStringLiteral("Compositor is not initialized."));
    if (d->context->thread() != QThread::currentThread())
        return fail(error, QStringLiteral("Compositor called from the wrong producer thread."));
    if (!d->context->makeCurrent(d->surface))
        return fail(error, QStringLiteral("Cannot activate the offscreen OpenGL context."));
    return true;
}

bool ImageCompositor::render(const QList<Layer> &layers, const QSize &canvas, QString *error)
{
    if (error) error->clear();
    // Check thread ownership before touching producer-only diagnostic state.
    if (d->context && d->context->thread() != QThread::currentThread())
        return fail(error, QStringLiteral("Compositor called from the wrong producer thread."));
    d->last = -1;
    if (canvas.isEmpty()) return fail(error, QStringLiteral("Canvas dimensions must be positive."));
    for (const auto &layer : layers) {
        if (layer.visible && layer.image.isNull())
            return fail(error, QStringLiteral("A visible layer has no decoded image."));
    }
    if (!makeCurrent(error)) return false;
    auto *gl = d->context->extraFunctions();
    GLint limit = 0;
    gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
    if (canvas.width() > limit || canvas.height() > limit) {
        d->context->doneCurrent();
        return fail(error, QStringLiteral("Canvas exceeds the OpenGL texture size limit."));
    }
    int target = -1;
    {
        QMutexLocker lock(&d->mutex);
        for (int i = 0; i < BufferCount; ++i) {
            auto &slot = d->buffers[i];
            if (slot.state == Private::Returning) {
                // Poll only: a slow consumer must not block the producer CPU.
                const GLenum status = gl->glClientWaitSync(slot.fence, 0, 0);
                if (status == GL_WAIT_FAILED) {
                    d->context->doneCurrent();
                    return fail(error, QStringLiteral("Cannot poll consumer completion fence."));
                }
                if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED) {
                    gl->glDeleteSync(slot.fence);
                    slot.fence = nullptr;
                    slot.state = Private::Idle;
                }
            }
            if (target < 0 && slot.state == Private::Idle) target = i;
        }
        // An unconsumed result may be replaced; an acquired result never may.
        if (target < 0) target = d->ready;
        if (target >= 0) {
            auto &slot = d->buffers[target];
            if (slot.fence) gl->glDeleteSync(slot.fence);
            slot.fence = nullptr;
            slot.state = Private::Writing;
            if (d->ready == target) d->ready = -1;
        }
    }
    if (target < 0) {
        d->context->doneCurrent();
        return false; // Normal backpressure: no new allocation, no error.
    }
    auto &slot = d->buffers[target];
    const auto abortFrame = [&](const QString &message) {
        if (slot.fbo) slot.fbo->release();
        {
            QMutexLocker lock(&d->mutex);
            slot.state = Private::Idle;
        }
        d->context->doneCurrent();
        return fail(error, message);
    };
    if (!slot.fbo || slot.fbo->size() != canvas) {
        slot.fbo.reset();
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        slot.fbo = std::make_unique<QOpenGLFramebufferObject>(canvas, format);
    }
    if (!slot.fbo->isValid() || !slot.fbo->bind())
        return abortFrame(QStringLiteral("Cannot create or bind the composition framebuffer."));
    gl->glViewport(0, 0, canvas.width(), canvas.height());
    bool painted = false;
    {
        QOpenGLPaintDevice device(canvas);
        QPainter painter(&device);
        if (painter.isActive()) {
            painter.fillRect(QRect(QPoint(), canvas), Qt::black);
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            for (auto it = layers.crbegin(); it != layers.crend(); ++it) {
                if (!it->visible) continue;
                const QSizeF fitted = QSizeF(it->image.size()).scaled(QSizeF(canvas), Qt::KeepAspectRatio);
                const QPointF origin((canvas.width() - fitted.width()) / 2,
                                     (canvas.height() - fitted.height()) / 2);
                painter.drawImage(QRectF(origin, fitted), it->image);
            }
            painted = true;
        }
    }
    if (!painted) return abortFrame(QStringLiteral("Cannot start the OpenGL paint engine."));
    slot.fbo->release();
    GLsync readyFence = nullptr;
    if (d->shared) {
        readyFence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!readyFence) return abortFrame(QStringLiteral("Cannot fence the composition frame."));
        gl->glFlush();
    }
    {
        QMutexLocker lock(&d->mutex);
        if (d->ready >= 0) {
            auto &old = d->buffers[d->ready];
            if (old.fence) gl->glDeleteSync(old.fence);
            old.fence = nullptr;
            old.state = Private::Idle;
        }
        slot.fence = readyFence;
        slot.token = nextToken.fetch_add(1);
        slot.state = Private::Ready;
        d->ready = target;
        d->last = target;
    }
    d->context->doneCurrent();
    return true;
}

unsigned int ImageCompositor::texture() const
{
    return d->last >= 0 ? d->buffers[d->last].fbo->texture() : 0;
}

QImage ImageCompositor::readback(QString *error)
{
    if (error) error->clear();
    if (d->last < 0) {
        fail(error, QStringLiteral("No completed composition frame."));
        return {};
    }
    if (!makeCurrent(error)) return {};
    const QImage image = d->buffers[d->last].fbo->toImage();
    d->context->doneCurrent();
    if (image.isNull()) fail(error, QStringLiteral("Cannot read the composition framebuffer."));
    return image;
}

ImageCompositor::Frame ImageCompositor::acquireFrame(QString *error)
{
    if (error) error->clear();
    QMutexLocker lock(&d->mutex);
    if (!d->consumerCurrent(error) || d->ready < 0) return {};
    auto &slot = d->buffers[d->ready];
    auto *current = QOpenGLContext::currentContext();
    auto *gl = current->extraFunctions();
    gl->glWaitSync(slot.fence, 0, GL_TIMEOUT_IGNORED);
    gl->glDeleteSync(slot.fence);
    slot.fence = nullptr;
    slot.consumer = current;
    slot.state = Private::Acquired;
    d->ready = -1;
    return {slot.fbo->texture(), slot.fbo->size(), slot.token};
}

bool ImageCompositor::returnFrame(const Frame &frame, QString *error)
{
    if (error) error->clear();
    QMutexLocker lock(&d->mutex);
    if (!d->consumerCurrent(error)) return false;
    for (auto &slot : d->buffers) {
        if (slot.state != Private::Acquired || slot.token != frame.token) continue;
        auto *current = QOpenGLContext::currentContext();
        if (slot.consumer != current || frame.texture != slot.fbo->texture()
            || frame.size != slot.fbo->size())
            return fail(error, QStringLiteral("Frame must be returned unchanged by its acquiring context."));
        auto *gl = current->extraFunctions();
        slot.fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!slot.fence) return fail(error, QStringLiteral("Cannot fence consumer completion."));
        gl->glFlush();
        slot.consumer = nullptr;
        slot.state = Private::Returning;
        return true;
    }
    return fail(error, QStringLiteral("Frame token is stale, foreign, or already returned."));
}

bool ImageCompositor::release(QString *error)
{
    if (error) error->clear();
    QMutexLocker lock(&d->mutex);
    if (!d->context) return true;
    for (const auto &slot : d->buffers) {
        if (slot.state == Private::Acquired)
            return fail(error, QStringLiteral("Return all acquired frames before releasing the compositor."));
    }
    if (!makeCurrent(error)) return false;
    auto *gl = d->context->extraFunctions();
    for (auto &slot : d->buffers) {
        if (slot.fence) {
            if (slot.state == Private::Returning)
                gl->glWaitSync(slot.fence, 0, GL_TIMEOUT_IGNORED);
            gl->glDeleteSync(slot.fence);
        }
        slot.fbo.reset();
        slot.fence = nullptr;
        slot.state = Private::Idle;
        slot.token = 0;
    }
    gl->glFlush();
    d->context->doneCurrent();
    d->context.reset();
    d->surface = nullptr;
    d->group = nullptr;
    d->shared = false;
    d->last = d->ready = -1;
    return true;
}
