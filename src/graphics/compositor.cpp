#include "graphics/videoformat.h"
#include "graphics/compositor.h"
#include "util/mediaclock.h"

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
    // 先在界面线程建立上下文共享；部分 WGL 驱动不允许在另一线程创建并共享仍由界面线程使用的上下文。
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
    // 原生离屏表面的创建与销毁必须留在界面线程。
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
        m_requestedReadbackId = m_completedReadbackId = 0;
        m_readbackImage = {};
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
    wait(); // 工作线程清理不依赖界面事件，因此这里等待不会形成循环等待。
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
    m_diagnosticOutputFrames.clear();
    m_droppedDiagnosticFrames = 0;
}

std::deque<ComposedImage> Compositor::takeOutput()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::exchange(m_diagnosticOutputFrames, {});
}

quint64 Compositor::droppedOutputFrames() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_droppedDiagnosticFrames;
}

void Compositor::setSources(const QList<RenderSourceSnapshot> &sources)
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
    const auto request = ++m_requestedReadbackId;
    m_wake.notify_all();
    if (!m_wake.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                         [&] {
                             return m_completedReadbackId >= request || m_stop || m_failed;
                         }) ||
        m_stop || m_failed)
        return {};
    return m_readbackImage;
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

        // 致命错误由下面的统一路径处理，确保等待读回的调用方也被唤醒。
        if (!renderer.initialize()) {
            fail(renderer.lastError());
            return; // 先析构渲染器，确保清理时 GL 上下文仍有效。
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
            slot.fbo = std::make_unique<QOpenGLFramebufferObject>(VideoFormat::size, format);
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
        const auto period = std::chrono::nanoseconds(VideoFormat::frameDurationNs);
        auto deadline = Clock::now();
        quint64 sequence = 0;
        while (true) {
            QList<RenderSourceSnapshot> sources;
            quint64 revision = 0, readback = 0;
            bool needsReadback = false, outputEnabled = false;
            qint64 timestampNs = 0;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                // 诊断请求可以提前唤醒，但不能改变正常输出的 30 Hz 节奏。
                m_wake.wait_until(lock, deadline, [&] {
                    return m_stop || m_requestedReadbackId > m_completedReadbackId;
                });
                if (m_stop)
                    break;
                sources = m_sources;
                revision = m_revision;
                readback = m_requestedReadbackId;
                needsReadback = readback > m_completedReadbackId;
                outputEnabled = m_outputEnabled || m_bus.hasSubscribers();
                // 时间戳对应本次取出的来源快照，不把后续渲染、同步和读回的耗时计入画面时间。
                timestampNs = MediaClock::nowNs();
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
                // 跨 GL 上下文发布前必须完成纹理写入；预览端同样要在归还租约前完成读取。
                context.functions()->glFinish();
                frame = std::make_shared<CompositionFrame>();
                frame->texture = slot.fbo->texture();
                frame->size = slot.fbo->size();
                frame->sequence = ++sequence;
                frame->revision = revision;
                frame->timestampNs = timestampNs;
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
                if (readback > m_completedReadbackId) {
                    m_readbackImage = image;
                    m_completedReadbackId = readback;
                }
                if (outputEnabled && !m_stop) {
                    m_bus.publish({image, timestampNs});
                    if (m_outputEnabled) {
                        m_diagnosticOutputFrames.push_back({image, timestampNs});
                        if (m_diagnosticOutputFrames.size() > 8) {
                            m_diagnosticOutputFrames.pop_front();
                            ++m_droppedDiagnosticFrames;
                        }
                    }
                }
            }
            m_wake.notify_all();
            // 错过的合成周期直接跳过，避免集中补帧；诊断触发的提前渲染也不改变正常输出期限。
            const auto now = Clock::now();
            if (deadline <= now)
                deadline += period * ((now - deadline) / period + 1);
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest.reset();
        }
        // 在释放当前 GL 上下文前，先销毁纹理槽、导入资源和着色器。
    }
    context.doneCurrent();
}
