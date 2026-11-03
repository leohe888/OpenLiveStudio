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
    const auto started = MediaClock::nowNs();
    while (!cancelled()) {
        m_images = m_videoTap->take();
        if (!m_images.empty()) {
            m_originNs = MediaClock::nowNs();
            m_selectedImage = m_images.back().image;
            m_selectedImageTimeNs = m_images.back().timestampNs;
            return true;
        }
        if (MediaClock::nowNs() - started > initialFrameTimeoutNs)
            throw QStringLiteral("等待合成画布超时。");
        QThread::msleep(initialFramePollMs);
    }
    return false;
}

bool OutputTimeline::read(qint64 frameIndex, QImage &image, QVector<float> &samples)
{
    collectPendingMedia();
    const auto now = MediaClock::nowNs();
    checkProducerHealth(now);
    const auto targetTimeNs = m_originNs + frameIndex * 1000000000LL / outputFramesPerSecond;
    if (now < targetTimeNs + synchronizationDelayNs)
        return false;
    selectVideoFrame(targetTimeNs, image);
    assembleAudioFrame(frameIndex, samples);
    return true;
}

void OutputTimeline::collectPendingMedia()
{
    for (auto &frame : m_videoTap->take())
        m_images.push_back(std::move(frame));
    for (auto &block : m_audioTap->take())
        m_audioBlocks.push_back(std::move(block));
    while (m_images.size() > maxVideoHistory)
        m_images.pop_front();
    if (m_audioBlocks.size() > maxAudioHistory || m_audioTap->dropped() != m_audioDropBaseline)
        throw QStringLiteral("音频输出缓冲已满。");
}

void OutputTimeline::checkProducerHealth(qint64 now)
{
    if (!m_video.isRunning() || !m_audio.isRunning()) {
        if (!m_producersInactiveSinceNs)
            m_producersInactiveSinceNs = now;
        if (now - m_producersInactiveSinceNs > producerStopGraceNs)
            throw QStringLiteral("画布合成或混音已停止。");
    } else
        m_producersInactiveSinceNs = 0;
}

void OutputTimeline::selectVideoFrame(qint64 targetTimeNs, QImage &image)
{
    // Choose the closest timestamp; reuse the last image when capture is slower.
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
        ++m_repeated;
    m_previousImageTimeNs = m_selectedImageTimeNs;
    image = m_selectedImage;
}

void OutputTimeline::assembleAudioFrame(qint64 frameIndex, QVector<float> &samples)
{
    constexpr int audioFramesPerVideoFrame = AudioFormat::rate / outputFramesPerSecond;
    static_assert(AudioFormat::rate % outputFramesPerSecond == 0);
    // Gaps stay silent; copy only the overlap with this encoder tick.
    const qint64 position = frameIndex * audioFramesPerVideoFrame;
    const qint64 end = position + audioFramesPerVideoFrame;
    samples.fill(0, audioFramesPerVideoFrame * AudioFormat::channels);
    qint64 supplied = 0;
    while (!m_audioBlocks.empty()) {
        const auto &block = *m_audioBlocks.front();
        const auto start = AudioFormat::nsToFrames(block.timestampNs - m_originNs),
                   finish = start + block.frames();
        if (start >= end)
            break;
        const auto first = std::max(position, start), last = std::min(end, finish);
        if (last > first) {
            std::copy_n(block.samples.constData() + (first - start) * AudioFormat::channels,
                        (last - first) * AudioFormat::channels,
                        samples.data() + (first - position) * AudioFormat::channels);
            supplied += last - first;
        }
        if (finish <= end)
            m_audioBlocks.pop_front();
        else
            break;
    }
    m_missingAudioFrames += std::max<qint64>(0, audioFramesPerVideoFrame - supplied);
}

void OutputTimeline::recover()
{
    m_images = m_videoTap->take();
    m_audioBlocks = m_audioTap->take();
    m_audioDropBaseline = m_audioTap->dropped();
}
