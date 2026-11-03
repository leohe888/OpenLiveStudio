#include "output/outputtimeline.h"
#include <QThread>

namespace {
constexpr qint64 initialFrameTimeoutNs = 3'000'000'000LL;
constexpr qint64 producerStopGraceNs = 500'000'000LL;
// Allow capture and mixing to arrive before selecting media for an encoder tick.
constexpr qint64 synchronizationDelayNs = 200'000'000LL;
constexpr int initialFramePollMs = 3;
constexpr size_t maxVideoHistory = 32;
constexpr size_t maxAudioHistory = 110;

}

bool OutputTimeline::initialize(const std::function<bool()> &cancelled)
{
    const auto waitStartedNs = MediaClock::nowNs();
    while (!cancelled()) {
        m_images = m_videoTap->takeAll();
        if (!m_images.empty()) {
            m_originNs = MediaClock::nowNs();
            m_selectedImage = m_images.back().image;
            m_selectedImageTimeNs = m_images.back().timestampNs;
            return true;
        }
        if (MediaClock::nowNs() - waitStartedNs > initialFrameTimeoutNs)
            throw QStringLiteral("等待合成画布超时。");
        QThread::msleep(initialFramePollMs);
    }
    return false;
}

bool OutputTimeline::read(qint64 frameIndex, QImage &image, QVector<float> &samples)
{
    collectPendingMedia();
    const auto nowNs = MediaClock::nowNs();
    checkProducerHealth(nowNs);
    const auto targetTimeNs = m_originNs + frameIndex * 1000000000LL / outputFramesPerSecond;
    if (nowNs < targetTimeNs + synchronizationDelayNs)
        return false;
    selectVideoFrame(targetTimeNs, image);
    assembleAudioFrame(frameIndex, samples);
    return true;
}

void OutputTimeline::collectPendingMedia()
{
    for (auto &frame : m_videoTap->takeAll())
        m_images.push_back(std::move(frame));
    for (auto &block : m_audioTap->takeAll())
        m_audioBlocks.push_back(std::move(block));
    while (m_images.size() > maxVideoHistory)
        m_images.pop_front();
    if (m_audioBlocks.size() > maxAudioHistory || m_audioTap->dropped() != m_audioDropCountBaseline)
        throw QStringLiteral("音频输出缓冲已满。");
}

void OutputTimeline::checkProducerHealth(qint64 nowNs)
{
    if (!m_video.isRunning() || !m_audio.isRunning()) {
        if (!m_producersInactiveSinceNs)
            m_producersInactiveSinceNs = nowNs;
        if (nowNs - m_producersInactiveSinceNs > producerStopGraceNs)
            throw QStringLiteral("画布合成或混音已停止。");
    } else
        m_producersInactiveSinceNs = 0;
}

void OutputTimeline::selectVideoFrame(qint64 targetTimeNs, QImage &image)
{
    // 采集帧率较低时复用上一图像，使编码输出仍保持固定节奏。
    while (!m_images.empty() && m_images.front().timestampNs <= targetTimeNs) {
        m_selectedImage = std::move(m_images.front().image);
        m_selectedImageTimeNs = m_images.front().timestampNs;
        m_images.pop_front();
    }
    if (!m_images.empty() && m_images.front().timestampNs - targetTimeNs <
                               targetTimeNs - m_selectedImageTimeNs) {
        m_selectedImage = std::move(m_images.front().image);
        m_selectedImageTimeNs = m_images.front().timestampNs;
        m_images.pop_front();
    }
    if (m_previousImageTimeNs == m_selectedImageTimeNs)
        ++m_repeatedVideoFrames;
    m_previousImageTimeNs = m_selectedImageTimeNs;
    image = m_selectedImage;
}

void OutputTimeline::assembleAudioFrame(qint64 frameIndex, QVector<float> &samples)
{
    constexpr int audioFramesPerVideoFrame = AudioFormat::rate / outputFramesPerSecond;
    static_assert(AudioFormat::rate % outputFramesPerSecond == 0);
    // 缺失区间保留静音，不压缩音频时间线，否则会逐渐与视频失步。
    const qint64 outputStartFrame = frameIndex * audioFramesPerVideoFrame;
    const qint64 outputEndFrame = outputStartFrame + audioFramesPerVideoFrame;
    samples.fill(0, audioFramesPerVideoFrame * AudioFormat::channels);
    qint64 suppliedAudioFrames = 0;
    while (!m_audioBlocks.empty()) {
        const auto &block = *m_audioBlocks.front();
        const auto blockStartFrame = AudioFormat::nsToFrames(block.timestampNs - m_originNs),
                   blockEndFrame = blockStartFrame + block.frames();
        if (blockStartFrame >= outputEndFrame)
            break;
        const auto overlapStartFrame = std::max(outputStartFrame, blockStartFrame),
                   overlapEndFrame = std::min(outputEndFrame, blockEndFrame);
        if (overlapEndFrame > overlapStartFrame) {
            std::copy_n(block.samples.constData() +
                            (overlapStartFrame - blockStartFrame) * AudioFormat::channels,
                        (overlapEndFrame - overlapStartFrame) * AudioFormat::channels,
                        samples.data() +
                            (overlapStartFrame - outputStartFrame) * AudioFormat::channels);
            suppliedAudioFrames += overlapEndFrame - overlapStartFrame;
        }
        if (blockEndFrame <= outputEndFrame)
            m_audioBlocks.pop_front();
        else
            break;
    }
    m_missingAudioFrames += std::max<qint64>(0, audioFramesPerVideoFrame - suppliedAudioFrames);
}

void OutputTimeline::recover()
{
    m_images = m_videoTap->takeAll();
    m_audioBlocks = m_audioTap->takeAll();
    m_audioDropCountBaseline = m_audioTap->dropped();
}
