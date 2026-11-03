#ifndef OUTPUTTIMELINE_H
#define OUTPUTTIMELINE_H
#include "graphics/compositor.h"
#include "audio/audiomixer.h"
#include "core/mediaclock.h"
#include "core/outputtap.h"
#include <functional>

// Aligns timestamped video and audio to the fixed encoder cadence.
// Each output worker owns its subscriptions, history and clock origin.
class OutputTimeline
{
public:
    OutputTimeline(Compositor &video, AudioMixer &audio)
        : m_video(video), m_audio(audio), m_videoTap(video.subscribeOutput()),
          m_audioTap(audio.subscribeOutput())
    {
    }

    bool initialize(const std::function<bool()> &cancelled);
    bool read(qint64 frameIndex, QImage &image, QVector<float> &samples);
    void recover(); // Drop old local history after a live-only timeline jump.

    qint64 originNs() const
    {
        return m_originNs;
    }

    quint64 droppedVideoFrames() const
    {
        return m_videoTap->dropped();
    }

    quint64 repeatedVideoFrames() const
    {
        return m_repeatedVideoFrames;
    }

    qint64 missingAudioFrames() const
    {
        return m_missingAudioFrames;
    }

    qint64 lagNs(qint64 frameIndex) const
    {
        return MediaClock::nowNs() -
               (m_originNs + frameIndex * 1000000000LL / outputFramesPerSecond);
    }

private:
    static constexpr int outputFramesPerSecond = 30;
    void collectPendingMedia();
    void checkProducerHealth(qint64 nowNs);
    void selectVideoFrame(qint64 targetTimeNs, QImage &image);
    void assembleAudioFrame(qint64 frameIndex, QVector<float> &samples);

    Compositor &m_video;
    AudioMixer &m_audio;
    std::shared_ptr<OutputTap<ComposedImage>> m_videoTap;
    std::shared_ptr<OutputTap<std::shared_ptr<const MixedAudioBlock>>> m_audioTap;
    std::deque<ComposedImage> m_images;
    std::deque<std::shared_ptr<const MixedAudioBlock>> m_audioBlocks;
    QImage m_selectedImage;
    qint64 m_originNs = 0;
    qint64 m_selectedImageTimeNs = 0;
    qint64 m_previousImageTimeNs = -1;
    qint64 m_producersInactiveSinceNs = 0;
    qint64 m_missingAudioFrames = 0;
    quint64 m_repeatedVideoFrames = 0;
    quint64 m_audioDropCountBaseline = 0;
};
#endif
