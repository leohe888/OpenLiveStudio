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
inline constexpr int blockFrames = 480; // 10 ms

inline qint64 framesToNs(qint64 frames)
{
    return frames * 1000000000LL / rate;
}

inline qint64 nsToFrames(qint64 ns)
{
    return qRound64(double(ns) * rate / 1000000000.0);
}
}

qint64 audioClockNs(); // Windows QPC time, nanoseconds; independent of wall clock.

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
    qint64 timestampNs = 0; // First stereo sample, QPC domain.
    QVector<float> samples; // Interleaved stereo, 48 kHz, normalized float.

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
        quint64 generation;
        std::deque<AudioBlock> packets;
    };

    void publish(AudioBlock block)
    {
        if (block.samples.isEmpty() || block.samples.size() % 2 || block.frames() > maxFrames)
            return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_frames += block.frames();
        m_packets.push_back(std::move(block));
        while (m_frames > maxFrames) {
            m_frames -= m_packets.front().frames();
            m_dropped += m_packets.front().frames();
            m_packets.pop_front();
        }
    }

    Batch take()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        Batch result{m_generation, {}};
        result.packets.swap(m_packets);
        m_frames = 0;
        return result;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_packets.clear();
        m_frames = 0;
        ++m_generation;
    }

    quint64 droppedFrames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

private:
    mutable std::mutex m_mutex;
    std::deque<AudioBlock> m_packets;
    int m_frames = 0;
    quint64 m_generation = 0, m_dropped = 0;
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
    AudioPeak peak; // Before final hard clipping.
    bool clipped = false;
};

#endif
