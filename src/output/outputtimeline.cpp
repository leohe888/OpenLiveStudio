#include "output/outputtimeline.h"
#include <functional>
bool OutputTimeline::initialize(const std::function<bool()> &cancelled)
{
    const auto started=MediaClock::nowNs();
    while (!cancelled()) {
        m_images=m_videoTap->take();
        if (!m_images.empty()) {
            m_origin=MediaClock::nowNs(); m_last=m_images.back().image;
            m_lastTime=m_images.back().timestampNs; return true;
        }
        if (MediaClock::nowNs()-started>3000000000LL) throw QStringLiteral("等待合成画布超时。");
        QThread::msleep(3);
    }
    return false;
}
bool OutputTimeline::read(qint64 frameIndex,QImage &image,QVector<float> &samples)
{
    for (auto &frame:m_videoTap->take()) m_images.push_back(std::move(frame));
    for (auto &block:m_audioTap->take()) m_audioBlocks.push_back(std::move(block));
    while (m_images.size()>32) m_images.pop_front();
    if (m_audioBlocks.size()>110 || m_audioTap->dropped()!=m_audioDropBaseline) throw QStringLiteral("音频输出缓冲已满。");
    const auto now=MediaClock::nowNs();
    if (!m_video.isRunning() || !m_audio.isRunning()) {
        if (!m_inactive) m_inactive=now;
        if (now-m_inactive>500000000LL) throw QStringLiteral("画布合成或混音已停止。");
    } else m_inactive=0;
    const auto time=m_origin+frameIndex*1000000000LL/30;
    if (now<time+200000000LL) return false;
    while (!m_images.empty() && m_images.front().timestampNs<=time) {
        m_last=std::move(m_images.front().image); m_lastTime=m_images.front().timestampNs; m_images.pop_front();
    }
    if (!m_images.empty() && m_images.front().timestampNs-time<time-m_lastTime) {
        m_last=std::move(m_images.front().image); m_lastTime=m_images.front().timestampNs; m_images.pop_front();
    }
    if (m_previousTime==m_lastTime) ++m_repeated;
    m_previousTime=m_lastTime; image=m_last;
    const qint64 position=frameIndex*1600,end=position+1600;
    samples.fill(0,3200); qint64 supplied=0;
    while (!m_audioBlocks.empty()) {
        const auto &block=*m_audioBlocks.front();
        const auto start=AudioFormat::nsToFrames(block.timestampNs-m_origin),finish=start+block.frames();
        if (start>=end) break;
        const auto first=std::max(position,start),last=std::min(end,finish);
        if (last>first) {
            std::copy_n(block.samples.constData()+(first-start)*2,(last-first)*2,
                samples.data()+(first-position)*2);
            supplied+=last-first;
        }
        if (finish<=end) m_audioBlocks.pop_front(); else break;
    }
    m_missing+=std::max<qint64>(0,1600-supplied); return true;
}
void OutputTimeline::recover()
{
    m_images=m_videoTap->take(); m_audioBlocks=m_audioTap->take();
    m_audioDropBaseline=m_audioTap->dropped();
}
