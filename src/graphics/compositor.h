#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "graphics/canvasrenderer.h"
#include <QThread>
#include "core/outputtap.h"
#include <QSurfaceFormat>
#include <condition_variable>
#include <mutex>
#include <deque>

class QOffscreenSurface;
class QOpenGLContext;

// An immutable lease on one of three shared output textures. A consumer must
// finish its GL reads before releasing the lease. Release all leases before stop().
// timestamp is composition time on the shared monotonic media clock (not capture PTS).
struct CompositionFrame {
    GLuint texture = 0;
    QSize size;
    quint64 sequence = 0;
    quint64 revision = 0;
    qint64 timestamp = 0;
    QHash<QString, RenderedSource> sources;
};

struct ComposedImage {
    QImage image;
    qint64 timestampNs = 0;
};

// GUI-thread control API; rendering, imports and GL cleanup run on this thread's
// worker. setSources replaces a single snapshot, never queues per-frame work.
class Compositor : public QThread
{
    Q_OBJECT
public:
    explicit Compositor(QObject *parent = nullptr);
    ~Compositor() override;
    bool startRendering(QOpenGLContext *shareContext = nullptr);
    void stopRendering();
    void setSources(const QList<PreviewSource> &sources);
    std::shared_ptr<const CompositionFrame> latestFrame() const;
    // Blocking diagnostic readback, not used by the preview or output hot path.
    // Returns a composition at least as new as the snapshot at call time.
    QImage captureImage(int timeoutMs = 3000);
    // Independent bounded CPU subscriptions. Read back once while any output
    // is subscribed. The legacy tap below is retained for diagnostics/tests.
    std::shared_ptr<OutputTap<ComposedImage>> subscribeOutput();
    void setOutputEnabled(bool enabled);
    std::deque<ComposedImage> takeOutput();
    quint64 droppedOutputFrames() const;

signals:
    void sourceFailed(const QString &sourceId, const QString &message);
    void renderingFailed(const QString &message);
    void gpuTransferFailed(const QString &sourceId, quint64 generation, const QString &message);

protected:
    void run() override;

private:
    OutputBus<ComposedImage> m_bus;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    QList<PreviewSource> m_sources;
    quint64 m_revision = 0;
    bool m_stop = true;
    bool m_failed = false;
    quint64 m_requestedReadback = 0, m_completedReadback = 0;
    QImage m_readback;
    std::shared_ptr<const CompositionFrame> m_latest;
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext> m_context;
    bool m_outputEnabled = false;
    std::deque<ComposedImage> m_output;
    quint64 m_droppedOutput = 0;
};

#endif
