#ifndef OUTPUTTIMELINE_H
#define OUTPUTTIMELINE_H
#include "preview/compositor.h"
#include "audio/audiomixer.h"
#include "core/mediaclock.h"
#include <functional>

// Worker-owned resampler. Independent subscriptions and origin for each output.
class OutputTimeline {
public:
    OutputTimeline(Compositor &video, AudioMixer &audio)
        : m_video(video), m_audio(audio), m_videoTap(video.subscribeOutput()), m_audioTap(audio.subscribeOutput()) {}
    bool initialize(const std::function<bool()> &cancelled);
    bool read(qint64 frameIndex, QImage &image, QVector<float> &samples);
    void recover(); // Drop old local history after a live-only timeline jump.
    qint64 origin() const { return m_origin; }
    quint64 dropped() const { return m_videoTap->dropped(); }
    quint64 repeated() const { return m_repeated; }
    qint64 missingAudio() const { return m_missing; }
    qint64 lag(qint64 index) const { return MediaClock::nowNs()-(m_origin+index*1000000000LL/30); }
private:
    Compositor &m_video; AudioMixer &m_audio;
    std::shared_ptr<OutputTap<ComposedImage>> m_videoTap;
    std::shared_ptr<OutputTap<std::shared_ptr<const MixedAudioBlock>>> m_audioTap;
    std::deque<ComposedImage> m_images;
    std::deque<std::shared_ptr<const MixedAudioBlock>> m_audioBlocks;
    QImage m_last;
    qint64 m_origin=0,m_lastTime=0,m_previousTime=-1,m_inactive=0,m_missing=0;
    quint64 m_repeated=0;
    quint64 m_audioDropBaseline=0;
};
#endif
