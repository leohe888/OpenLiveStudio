#ifndef OUTPUTTIMELINE_H
#define OUTPUTTIMELINE_H
#include "graphics/compositor.h"
#include "audio/audiomixer.h"
#include "util/mediaclock.h"
#include "util/outputtap.h"
#include <functional>

// 将带时间戳的音视频对齐到编码器的固定输出节奏。
// 各输出线程独占订阅、历史缓冲和时间原点，录制与直播不会争抢数据或相互重置。
class OutputTimeline
{
public:
    OutputTimeline(Compositor &video, AudioMixer &audio)
        : m_video(video), m_audio(audio), m_videoTap(video.subscribeOutput()),
          m_audioTap(audio.subscribeOutput())
    {
    }

    // 取消时返回 false；等不到首帧则抛出错误，避免输出线程无限等待。
    bool initialize(const std::function<bool()> &cancelled);
    // false 表示同步窗口尚未到达，调用方应稍后重试同一帧序号。
    bool read(qint64 frameIndex, QImage &image, QVector<float> &samples);
    void recover(); // 直播跳过积压后丢弃旧历史；录制不能用此接口跳过内容。

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
