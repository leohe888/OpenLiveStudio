#include "recordingcontroller.h"
#include "mp4encoder.h"
#include "preview/compositor.h"
#include "audio/audiomixer.h"
#include "core/mediaclock.h"
#include <QFileInfo>
#include <QScopeGuard>
#include <atomic>

class RecordingWorker : public QThread {
public:
    RecordingWorker(Compositor &video, AudioMixer &audio, QString path, int crf)
        : m_video(video), m_audio(audio), m_crf(crf) { m_status.state="starting"; m_status.path=std::move(path); }
    ~RecordingWorker() override { requestStop(); wait(); }
    void requestStop() { qint64 expected=0; m_stopAt.compare_exchange_strong(expected,MediaClock::nowNs()); }
    RecordingStatus snapshot() const { std::lock_guard<std::mutex> lock(m_mutex); return m_status; }
protected:
    void run() override
    {
        Mp4Encoder encoder;
        RecordingStatus result=snapshot();
        const auto reset = qScopeGuard([&] { m_video.setOutputEnabled(false); m_audio.setOutputEnabled(false); });
        // Deterministic acceptance-test hook; never enabled by the product UI.
        const int artificialDelay=std::clamp(qEnvironmentVariableIntValue("OPENLIVE_RECORDING_ENCODER_DELAY_MS"),0,1000);
        try {
            if (m_stopAt.load()) { result.state="idle"; publish(result); return; }
            encoder.open(result.path,m_crf);
            m_video.setOutputEnabled(true); m_audio.setOutputEnabled(true);
            std::deque<ComposedImage> images;
            const auto initialized = MediaClock::nowNs();
            while (images.empty() && !m_stopAt.load()) {
                images=m_video.takeOutput();
                if (MediaClock::nowNs()-initialized > 3000000000LL) throw QStringLiteral("等待合成画布超时。");
                if (images.empty()) QThread::msleep(3);
            }
            if (m_stopAt.load()) { result.state="idle"; publish(result); return; }
            const qint64 origin = MediaClock::nowNs();
            QImage lastImage=images.back().image;
            qint64 lastImageTime=images.back().timestampNs, previousImageTime=-1;
            std::deque<std::shared_ptr<const MixedAudioBlock>> audio;
            qint64 audioPosition=0, inactiveSince=0;
            result.state="recording"; publish(result);
            while (true) {
                for (auto &image : m_video.takeOutput()) images.push_back(std::move(image));
                for (auto &block : m_audio.takeOutput()) audio.push_back(std::move(block));
                while (images.size()>16) images.pop_front();
                if (audio.size()>110 || m_audio.droppedOutputBlocks()) throw QStringLiteral("音频编码缓冲已满，录制已停止。");
                const qint64 now=MediaClock::nowNs();
                if (!m_video.isRunning()) {
                    if (!inactiveSince) inactiveSince=now;
                    if (now-inactiveSince>500000000LL) throw QStringLiteral("画布合成已停止，无法继续录制。");
                } else inactiveSince=0;
                const auto stop=m_stopAt.load();
                const qint64 finalFrames=stop ? std::max<qint64>(1,((stop-origin)*30+999999999LL)/1000000000LL) : 0;
                if (stop) result.state="stopping";
                if (stop && result.videoFrames>=finalFrames) break;
                const qint64 frameTime=origin+result.videoFrames*1000000000LL/30;
                // The mixer intentionally runs 60 ms behind the capture clock;
                // retain enough video history to absorb scheduling/readback jitter.
                if (now < frameTime+200000000LL) { publishProgress(result,origin,stop); QThread::msleep(3); continue; }
                if (now-frameTime>1000000000LL) throw QStringLiteral("编码持续落后超过一秒，录制已停止。");
                while (!images.empty() && images.front().timestampNs<=frameTime) {
                    lastImage=std::move(images.front().image); lastImageTime=images.front().timestampNs; images.pop_front();
                }
                // Resample to the nearest available snapshot, avoiding an extra
                // frame of systematic video delay when the two 30 Hz clocks differ.
                if (!images.empty() && images.front().timestampNs-frameTime < frameTime-lastImageTime) {
                    lastImage=std::move(images.front().image); lastImageTime=images.front().timestampNs; images.pop_front();
                }
                if (lastImageTime==previousImageTime) ++result.repeatedFrames;
                previousImageTime=lastImageTime;
                encoder.video(lastImage,result.videoFrames);
                ++result.videoFrames;
                const qint64 end=result.videoFrames*1600; // 48 kHz / 30 FPS, exact.
                QVector<float> samples(int(end-audioPosition)*2,0);
                qint64 supplied=0;
                while (!audio.empty()) {
                    const auto &block=*audio.front();
                    const qint64 start=AudioFormat::nsToFrames(block.timestampNs-origin);
                    const qint64 finish=start+block.frames();
                    if (start>=end) break;
                    const qint64 first=std::max(audioPosition,start), last=std::min(end,finish);
                    if (last>first) {
                        for (qint64 i=first; i<last; ++i) {
                            samples[int(i-audioPosition)*2]=block.samples[int(i-start)*2];
                            samples[int(i-audioPosition)*2+1]=block.samples[int(i-start)*2+1];
                        }
                        supplied+=last-first;
                    }
                    if (finish<=end) audio.pop_front(); else break;
                }
                result.missingAudioSamples+=std::max<qint64>(0,end-audioPosition-supplied);
                encoder.audio(samples); audioPosition=end;
                if (artificialDelay) QThread::msleep(unsigned(artificialDelay));
                result.droppedFrames=m_video.droppedOutputFrames();
                publishProgress(result,origin,stop);
            }
            encoder.finish(); result.fileComplete=true; result.state="idle";
        } catch (const QString &message) {
            result.error=message; result.state="error";
            if (result.videoFrames) {
                try { encoder.finish(); result.fileComplete=true; }
                catch (const QString &finalError) { result.error+=QStringLiteral("\n%1").arg(finalError); }
            }
        } catch (const std::exception &exception) {
            result.state="error"; result.error=QStringLiteral("录制失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        if (result.videoFrames) result.durationMs=result.videoFrames*1000/30;
        publish(result);
    }
private:
    void publish(const RecordingStatus &status) { std::lock_guard<std::mutex> lock(m_mutex); m_status=status; }
    void publishProgress(RecordingStatus &status,qint64 origin,qint64 stop)
    { status.durationMs=std::max<qint64>(0,((stop ? stop : MediaClock::nowNs())-origin)/1000000); publish(status); }
    Compositor &m_video; AudioMixer &m_audio;
    int m_crf;
    std::atomic<qint64> m_stopAt{0};
    mutable std::mutex m_mutex;
    RecordingStatus m_status;
};

RecordingController::RecordingController(Compositor &video,AudioMixer &audio,QObject *parent)
    : QObject(parent), m_video(video), m_audio(audio)
{
    m_poll.setInterval(50); connect(&m_poll,&QTimer::timeout,this,&RecordingController::poll); m_poll.start();
    for (auto *thread : {static_cast<QThread *>(&m_video),static_cast<QThread *>(&m_audio)}) {
        connect(thread,&QThread::started,this,&RecordingController::availabilityChanged);
        connect(thread,&QThread::finished,this,&RecordingController::availabilityChanged);
    }
}
bool RecordingController::ready() const { return m_video.isRunning() && m_audio.isRunning(); }
RecordingController::~RecordingController() { m_poll.stop(); if (m_worker) { m_worker->requestStop(); m_worker->wait(); } }
bool RecordingController::startRecording(const QString &path,int quality)
{
    if (busy() || (m_worker && m_worker->isRunning())) return false;
    if (!m_video.isRunning() || !m_audio.isRunning() || !QFileInfo(path).isAbsolute()
        || QFileInfo(path).suffix().compare("mp4",Qt::CaseInsensitive) || quality<0 || quality>2) {
        m_status={}; m_status.state="error"; m_status.error=tr("合成或混音尚未就绪，或录制路径/质量无效。");
        emit statusChanged(); return false;
    }
    const int crf[]{23,20,17};
    m_worker=std::make_unique<RecordingWorker>(m_video,m_audio,path,crf[quality]);
    connect(m_worker.get(),&QThread::finished,this,&RecordingController::poll,Qt::QueuedConnection);
    m_status=m_worker->snapshot(); emit statusChanged(); m_worker->start(); return true;
}
void RecordingController::stopRecording()
{
    if (!busy() || !m_worker) return;
    m_worker->requestStop(); m_status.state="stopping"; emit statusChanged();
}
void RecordingController::poll()
{
    if (!m_worker) return;
    const bool wasBusy=busy();
    m_status=m_worker->snapshot();
    // Completion is observable only after output taps and worker-owned resources close.
    if (!m_worker->isFinished() && (m_status.state=="idle" || m_status.state=="error")) m_status.state="stopping";
    emit statusChanged();
    if (wasBusy && !busy()) emit finished();
}
