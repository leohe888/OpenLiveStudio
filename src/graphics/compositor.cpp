#include "graphics/compositor.h"
#include "core/mediaclock.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QScopeGuard>
#include <array>
#include <chrono>
#include <utility>

Compositor::Compositor(QObject *parent) : QThread(parent)
{
}

Compositor::~Compositor()
{
    stopRendering();
}

bool Compositor::startRendering(QOpenGLContext *shareContext)
{
    Q_ASSERT(QThread::currentThread() == thread());
    stopRendering();
    auto *previousContext = QOpenGLContext::currentContext();
    auto *previousSurface = previousContext ? previousContext->surface() : nullptr;
    const auto restoreContext = qScopeGuard([&] {
        if (previousContext && previousSurface)
            previousContext->makeCurrent(previousSurface);
    });
    // Establish sharing on the GUI thread. WGL drivers may reject creation on
    // another thread while the share context is current on the GUI thread.
    m_context = std::make_unique<QOpenGLContext>();
    m_context->setFormat(shareContext ? shareContext->format() : QSurfaceFormat::defaultFormat());
    if (shareContext)
        m_context->setShareContext(shareContext);
    if (!m_context->create() ||
        (shareContext && !QOpenGLContext::areSharing(m_context.get(), shareContext))) {
        emit renderingFailed(tr("无法创建独立 OpenGL 合成上下文或共享纹理。"));
        m_context.reset();
        return false;
    }
    // Native offscreen surfaces must be created and destroyed on the GUI thread.
    m_surface = std::make_unique<QOffscreenSurface>();
    m_surface->setFormat(m_context->format());
    m_surface->create();
    if (!m_surface->isValid()) {
        emit renderingFailed(tr("无法创建离屏合成表面。"));
        m_surface.reset();
        m_context.reset();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = false;
        m_failed = false;
        m_requestedReadback = m_completedReadback = 0;
        m_readback = {};
    }
    m_context->moveToThread(this);
    start();
    return true;
}

void Compositor::stopRendering()
{
    Q_ASSERT(QThread::currentThread() == thread());
    auto *previousContext = QOpenGLContext::currentContext();
    auto *previousSurface = previousContext ? previousContext->surface() : nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_latest.reset();
    }
    m_wake.notify_all();
    wait(); // Worker cleanup never waits for GUI events.
    m_surface.reset();
    if (previousContext && previousSurface)
        previousContext->makeCurrent(previousSurface);
}

std::shared_ptr<OutputTap<ComposedImage>> Compositor::subscribeOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bus.subscribe(32);
}

void Compositor::setOutputEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_outputEnabled = enabled;
    m_output.clear();
    m_droppedOutput = 0;
}

std::deque<ComposedImage> Compositor::takeOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_output, {});
}

quint64 Compositor::droppedOutputFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_droppedOutput;
}

void Compositor::setSources(const QList<PreviewSource> &sources)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sources = sources;
    for (auto &source : m_sources)
        if (source.transform.size().isEmpty())
            source.transform = VideoSourceTransform(source.pixelSize());
    ++m_revision;
}

std::shared_ptr<const CompositionFrame> Compositor::latestFrame() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_latest;
}

QImage Compositor::captureImage(int timeoutMs)
{
    Q_ASSERT(QThread::currentThread() != this);
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_stop || m_failed)
        return {};
    const auto request = ++m_requestedReadback;
    m_wake.notify_all();
    if (!m_wake.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&] {
                             return m_completedReadback >= request || m_stop || m_failed;
                         }) ||
        m_stop || m_failed)
        return {};
    return m_readback;
}

void Compositor::run()
{
    const auto fail = [this](const QString &message) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_failed = true;
        }
        m_wake.notify_all();
        emit renderingFailed(message);
    };
    auto contextOwner = std::move(m_context);
    auto &context = *contextOwner;
    if (!context.makeCurrent(m_surface.get())) {
        fail(tr("无法激活独立 OpenGL 合成上下文。"));
        return;
    }
    {
        CanvasRenderer renderer;
        connect(&renderer, &CanvasRenderer::sourceFailed, this, &Compositor::sourceFailed,
                Qt::DirectConnection);
        connect(&renderer, &CanvasRenderer::gpuTransferFailed, this, &Compositor::gpuTransferFailed,
                Qt::DirectConnection);
        // Fatal errors are handled below so pending readbacks also wake up.
        if (!renderer.initialize()) {
            fail(renderer.lastError());
            return; // Renderer is destroyed before its current context.
        }

        struct OutputSlot
        {
            std::unique_ptr<QOpenGLFramebufferObject> fbo;
            std::weak_ptr<const CompositionFrame> lease;
        };

        std::array<OutputSlot, 3> outputSlots;
        QOpenGLFramebufferObjectFormat format;
        format.setInternalTextureFormat(GL_RGBA8);
        for (auto &slot : outputSlots) {
            slot.fbo = std::make_unique<QOpenGLFramebufferObject>(QSize(1920, 1080), format);
            if (!slot.fbo->isValid()) {
                fail(tr("无法创建合成输出缓冲。"));
                return;
            }
            auto *gl = context.functions();
            gl->glBindTexture(GL_TEXTURE_2D, slot.fbo->texture());
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        using Clock = std::chrono::steady_clock;
        const auto period = std::chrono::nanoseconds(1000000000 / 30);
        auto deadline = Clock::now();
        quint64 sequence = 0;
        while (true) {
            QList<PreviewSource> sources;
            quint64 revision = 0, readback = 0;
            bool needsReadback = false, outputEnabled = false;
            qint64 timestamp = 0;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // Diagnostic requests may wake early; normal output stays at 30 Hz.
                m_wake.wait_until(lock, deadline, [&] {
                    return m_stop || m_requestedReadback > m_completedReadback;
                });
                if (m_stop)
                    break;
                sources = m_sources;
                revision = m_revision;
                readback = m_requestedReadback;
                needsReadback = readback > m_completedReadback;
                outputEnabled = m_outputEnabled || m_bus.active();
                // The image represents this source snapshot. GPU rendering,
                // cross-context synchronization and readback can finish later.
                timestamp = MediaClock::nowNs();
            }
            if (!renderer.render(sources)) {
                fail(renderer.lastError());
                break;
            }
            std::shared_ptr<CompositionFrame> frame;
            for (auto &slot : outputSlots) {
                if (!slot.lease.expired())
                    continue;
                QOpenGLFramebufferObject::blitFramebuffer(slot.fbo.get(), renderer.canvas());
                if (context.functions()->glGetError() != GL_NO_ERROR) {
                    fail(tr("无法复制合成输出画布。"));
                    break;
                }
                // Complete producer writes before publishing across GL contexts.
                // The preview likewise finishes reads before returning its lease.
                context.functions()->glFinish();
                frame = std::make_shared<CompositionFrame>();
                frame->texture = slot.fbo->texture();
                frame->size = slot.fbo->size();
                frame->sequence = ++sequence;
                frame->revision = revision;
                frame->timestamp = timestamp;
                frame->sources = renderer.renderedSources();
                slot.lease = frame;
                break;
            }
            const QImage image =
                needsReadback || outputEnabled ? renderer.canvas()->toImage() : QImage();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_failed)
                    break;
                if (!m_stop && frame)
                    m_latest = std::move(frame);
                if (readback > m_completedReadback) {
                    m_readback = image;
                    m_completedReadback = readback;
                }
                if (outputEnabled && !m_stop) {
                    m_bus.publish({image, timestamp});
                    if (m_outputEnabled) {
                        m_output.push_back({image, timestamp});
                        if (m_output.size() > 8) {
                            m_output.pop_front();
                            ++m_droppedOutput;
                        }
                    }
                }
            }
            m_wake.notify_all();
            // Skip missed deadlines rather than enqueueing a catch-up burst.
            // An early diagnostic render must not push normal output into the future.
            const auto now = Clock::now();
            if (deadline <= now)
                deadline += period * ((now - deadline) / period + 1);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest.reset();
        }
        // Slots, imports, textures and shaders are destroyed with this context current.
    }
    context.doneCurrent();
}
