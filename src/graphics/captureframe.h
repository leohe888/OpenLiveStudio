#ifndef CAPTUREFRAME_H
#define CAPTUREFRAME_H

#include <QSize>
#include <QByteArray>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

struct GpuFrameResource;

// CPU 帧保持原始像素布局；颜色转换由 GPU 执行。
struct PixelFrame
{
    enum Layout {
        Planar,
        NV12,
        YUY2,
        UYVY,
        RGB,
        BGR,
        RGBA,
        BGRA
    };

    Layout layout = Planar;
    std::array<QByteArray, 3> planes;
    std::array<QSize, 3> planeSizes;
    bool fullRange = false;
    bool bt709 = false;
};

struct CaptureFrame
{
    QSize size;
    qint64 timestampNs = 0; // 采集时刻，与后续合成时刻分别记录。
    quint64 sequence = 0;
    quint64 generation = 0;
    std::shared_ptr<GpuFrameResource> gpu;
    std::shared_ptr<const PixelFrame> pixels;
    std::shared_ptr<std::atomic_bool> poolSlotInUse;

    ~CaptureFrame()
    {
        if (poolSlotInUse)
            poolSlotInUse->store(false, std::memory_order_release);
    }

    CaptureFrame() = default;
    CaptureFrame(const CaptureFrame &) = delete;
    CaptureFrame &operator=(const CaptureFrame &) = delete;
};

// 锁内只交换帧的所有权，避免像素复制或 GPU 操作占用临界区。
class LatestCaptureFrameSlot
{
public:
    void publish(std::shared_ptr<const CaptureFrame> frame)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest.swap(frame);
        }
    }

    std::shared_ptr<const CaptureFrame> takeLatest()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return std::exchange(m_latest, {});
    }

    void clear()
    {
        publish({});
    }

private:
    std::mutex m_mutex;
    std::shared_ptr<const CaptureFrame> m_latest;
};

#endif
