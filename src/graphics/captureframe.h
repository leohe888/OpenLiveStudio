#ifndef CAPTUREFRAME_H
#define CAPTUREFRAME_H

#include <QSize>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

struct GpuFrameResource;

struct CaptureFrame
{
    QSize size;
    qint64 timestamp = 0; // Monotonic capture time, in nanoseconds.
    quint64 sequence = 0;
    quint64 generation = 0;
    std::shared_ptr<GpuFrameResource> gpu;
    std::shared_ptr<std::atomic_bool> busy;

    ~CaptureFrame()
    {
        if (busy)
            busy->store(false, std::memory_order_release);
    }

    CaptureFrame() = default;
    CaptureFrame(const CaptureFrame &) = delete;
    CaptureFrame &operator=(const CaptureFrame &) = delete;
};

// The lock protects ownership exchange, never pixel copies or GPU operations.
class LatestCaptureFrame
{
public:
    void publish(std::shared_ptr<const CaptureFrame> frame)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latest.swap(frame);
        }
    }

    std::shared_ptr<const CaptureFrame> take()
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
