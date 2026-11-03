#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H

#include <QHash>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <utility>

namespace AudioFormat {
inline constexpr int rate = 48000;
inline constexpr int channels = 2;
inline constexpr int blockFrames = 480; // 每块覆盖 10 ms，作为混音的处理步长。

inline qint64 framesToNs(qint64 frames)
{
    return frames * 1000000000LL / rate;
}

inline qint64 nsToFrames(qint64 ns)
{
    return qRound64(double(ns) * rate / 1000000000.0);
}
}

qint64 audioClockNs(); // 与视频共用单调媒体时钟，不受系统校时影响。

struct AudioPeak
{
    float left = 0, right = 0;

    void add(const AudioPeak &other)
    {
        left = std::max(left, other.left);
        right = std::max(right, other.right);
    }
};

struct AudioBlock
{
    qint64 timestampNs = 0; // 首个音频帧的单调媒体时钟时间，用于跨设备对齐。
    QVector<float> samples; // 48 kHz 归一化浮点数，按左、右声道交错排列；两个值构成一个音频帧。

    int frames() const
    {
        return int(samples.size() / AudioFormat::channels);
    }
};

class AudioPacketQueue
{
public:
    static constexpr int maxFrames = AudioFormat::rate / 2;

    struct Batch
    {
        quint64 generation; // clear 后代次变化，混音器据此丢弃旧会话的本地缓存。
        std::deque<AudioBlock> packets;
    };

    void publish(AudioBlock block)
    {
        if (block.samples.isEmpty() || block.samples.size() % 2 || block.frames() > maxFrames)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queuedFrames += block.frames();
        m_packets.push_back(std::move(block));
        while (m_queuedFrames > maxFrames) {
            m_queuedFrames -= m_packets.front().frames();
            m_droppedFrames += m_packets.front().frames();
            m_packets.pop_front();
        }
    }

    Batch takeAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Batch result{m_generation, {}};
        result.packets.swap(m_packets);
        m_queuedFrames = 0;
        return result;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_packets.clear();
        m_queuedFrames = 0;
        ++m_generation;
    }

    quint64 droppedFrames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_droppedFrames;
    }

private:
    mutable std::mutex m_mutex;
    std::deque<AudioBlock> m_packets;
    int m_queuedFrames = 0;
    quint64 m_generation = 0, m_droppedFrames = 0;
};

struct AudioInput
{
    QString id;
    float gain = 1;
    bool muted = false;
    std::shared_ptr<AudioPacketQueue> queue;
};

struct MixedAudioBlock : AudioBlock
{
    quint64 sequence = 0;
    AudioPeak peak; // 保留限幅前的峰值，使电平表能显示过载。
    bool clipped = false;
};

#endif
