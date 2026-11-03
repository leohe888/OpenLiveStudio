#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "graphics/canvasrenderer.h"
#include <QThread>
#include "core/outputbus.h"
#include <QSurfaceFormat>
#include <condition_variable>
#include <mutex>
#include <deque>

class QOffscreenSurface;
class QOpenGLContext;

// 持有此对象即占用一个共享纹理槽；消费者必须完成 GL 读取后再释放，防止生产者覆盖纹理。
// 调用 stopRendering() 前应释放所有租约。timestampNs 属于单调媒体时钟，表示合成时间。
struct CompositionFrame
{
    GLuint texture = 0;
    QSize size;
    quint64 sequence = 0;
    quint64 revision = 0;
    qint64 timestampNs = 0;
    QHash<QString, RenderedSource> sources;
};

struct ComposedImage
{
    QImage image;
    qint64 timestampNs = 0;
};

// 控制接口在界面线程调用；渲染、纹理导入和 GL 清理由工作线程完成。
// setSources 只替换最新快照，避免按采集帧堆积渲染任务。
class Compositor : public QThread
{
    Q_OBJECT
public:
    explicit Compositor(QObject *parent = nullptr);
    ~Compositor() override;
    bool startRendering(QOpenGLContext *shareContext = nullptr);
    void stopRendering();
    void setSources(const QList<RenderSourceSnapshot> &sources);
    std::shared_ptr<const CompositionFrame> latestFrame() const;
    // 阻塞式诊断读回，不用于预览或编码主流程；返回结果至少包含调用时的来源快照。
    QImage captureImage(int timeoutMs = 3000);
    // CPU 输出按消费者独立订阅；存在订阅时，每次合成只读回一次并共享图像。
    // 下方的单队列接口仅保留给诊断和测试使用。
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
    QList<RenderSourceSnapshot> m_sources;
    quint64 m_revision = 0;
    bool m_stop = true;
    bool m_failed = false;
    quint64 m_requestedReadbackId = 0, m_completedReadbackId = 0;
    QImage m_readbackImage;
    std::shared_ptr<const CompositionFrame> m_latest;
    std::unique_ptr<QOffscreenSurface> m_surface;
    std::unique_ptr<QOpenGLContext> m_context;
    bool m_outputEnabled = false;
    std::deque<ComposedImage> m_diagnosticOutputFrames;
    quint64 m_droppedDiagnosticFrames = 0;
};

#endif
