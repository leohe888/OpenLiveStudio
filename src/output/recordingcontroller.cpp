#include "output/recordingcontroller.h"
#include "output/mediaencoder.h"
#include "output/outputtimeline.h"
#include "graphics/compositor.h"
#include "audio/audiomixer.h"
#include "core/mediaclock.h"
#include <QFileInfo>
#include <atomic>

class RecordingWorker : public QThread
{
public:
    RecordingWorker(Compositor &video, AudioMixer &audio, QString path, int bitrateKbps)
        : m_video(video), m_audio(audio), m_bitrateKbps(bitrateKbps)
    {
        m_status.state = "starting";
        m_status.path = std::move(path);
    }

    ~RecordingWorker() override
    {
        requestStop();
        wait();
    }

    void requestStop()
    {
        qint64 expected = 0;
        m_stopAtNs.compare_exchange_strong(expected, MediaClock::nowNs());
    }

    RecordingStatus snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_status;
    }

protected:
    void run() override
    {
        MediaEncoder encoder;
        RecordingStatus result = snapshot();
        // Deterministic acceptance-test hook; never enabled by the product UI.
        const int artificialEncodeDelayMs = std::clamp(
            qEnvironmentVariableIntValue("OPENLIVE_RECORDING_ENCODER_DELAY_MS"), 0, 1000);
        const int artificialPauseOnceMs =
            std::clamp(qEnvironmentVariableIntValue("OPENLIVE_RECORDING_PAUSE_ONCE_MS"), 0, 800);
        try {
            if (m_stopAtNs.load()) {
                result.state = "idle";
                publish(result);
                return;
            }
            encoder.open(result.path, m_bitrateKbps);
            OutputTimeline timeline(m_video, m_audio);
            if (!timeline.initialize([&] {
                    return m_stopAtNs.load() != 0;
                })) {
                result.state = "idle";
                publish(result);
                return;
            }
            const auto originNs = timeline.originNs();
            result.state = "recording";
            publish(result);
            qint64 pressureSinceNs = 0, pressureBaselineNs = 0;
            while (true) {
                const auto stopTimeNs = m_stopAtNs.load();
                const qint64 finalFrames =
                    stopTimeNs ? std::max<qint64>(1, ((stopTimeNs - originNs) * 30 + 999999999LL) /
                                                         1000000000LL)
                               : 0;
                if (stopTimeNs)
                    result.state = "stopping";
                if (stopTimeNs && result.videoFrames >= finalFrames)
                    break;
                QImage image;
                QVector<float> samples;
                if (!timeline.read(result.videoFrames, image, samples)) {
                    publishProgress(result, originNs, stopTimeNs);
                    QThread::msleep(3);
                    continue;
                }
                result.lagMs = timeline.lagNs(result.videoFrames) / 1000000;
                // A backlog that is draining after a scheduling/IO hiccup is
                // not sustained overload. Only shed streaming when the backlog
                // stays high and fails to decrease over a 150 ms observation.
                const auto lagNs = timeline.lagNs(result.videoFrames), nowNs = MediaClock::nowNs();
                if (lagNs <= 650000000LL) {
                    pressureSinceNs = 0;
                    result.pressured = false;
                } else if (!pressureSinceNs) {
                    pressureSinceNs = nowNs;
                    pressureBaselineNs = lagNs;
                } else if (nowNs - pressureSinceNs > 150000000LL) {
                    result.pressured = lagNs >= pressureBaselineNs;
                    pressureSinceNs = nowNs;
                    pressureBaselineNs = lagNs;
                }
                if (timeline.lagNs(result.videoFrames) > 1000000000LL)
                    throw QStringLiteral("编码持续落后超过一秒，录制已停止。");
                const auto encodeStartNs = MediaClock::nowNs(),
                           outputTimeBeforeNs = encoder.outputTimeNs();
                encoder.encodeVideoFrame(image, result.videoFrames);
                const auto audioStartNs = MediaClock::nowNs(),
                           outputTimeAfterVideoNs = encoder.outputTimeNs();
                result.videoEncodeMs =
                    (audioStartNs - encodeStartNs - (outputTimeAfterVideoNs - outputTimeBeforeNs)) /
                    1000000;
                result.conversionMs = encoder.lastConversionNs() / 1000000;
                result.imageFormat = int(image.format());
                encoder.encodeAudioSamples(samples);
                result.audioEncodeMs = (MediaClock::nowNs() - audioStartNs -
                                        (encoder.outputTimeNs() - outputTimeAfterVideoNs)) /
                                       1000000;
                result.lastOutputMs = (encoder.outputTimeNs() - outputTimeBeforeNs) / 1000000;
                result.lastEncodeMs =
                    (MediaClock::nowNs() - encodeStartNs) / 1000000 - result.lastOutputMs;
                ++result.videoFrames;
                result.repeatedFrames = timeline.repeatedVideoFrames();
                result.missingAudioFrames = timeline.missingAudioFrames();
                result.droppedFrames = timeline.droppedVideoFrames();
                if (artificialEncodeDelayMs)
                    QThread::msleep(unsigned(artificialEncodeDelayMs));
                if (artificialPauseOnceMs && result.videoFrames == 6)
                    QThread::msleep(unsigned(artificialPauseOnceMs));
                publishProgress(result, originNs, stopTimeNs);
            }
            encoder.finish();
            result.fileComplete = true;
            result.state = "idle";
        } catch (const QString &message) {
            result.error = message;
            result.state = "error";
            if (result.videoFrames) {
                try {
                    encoder.finish();
                    result.fileComplete = true;
                } catch (const QString &finalError) {
                    result.error += QStringLiteral("\n%1").arg(finalError);
                }
            }
        } catch (const std::exception &exception) {
            result.state = "error";
            result.error = QStringLiteral("录制失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        if (result.videoFrames)
            result.durationMs = result.videoFrames * 1000 / 30;
        publish(result);
    }

private:
    void publish(const RecordingStatus &status)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = status;
    }

    void publishProgress(RecordingStatus &status, qint64 originNs, qint64 stopTimeNs)
    {
        status.durationMs = std::max<qint64>(
            0, ((stopTimeNs ? stopTimeNs : MediaClock::nowNs()) - originNs) / 1000000);
        publish(status);
    }

    Compositor &m_video;
    AudioMixer &m_audio;
    int m_bitrateKbps;
    std::atomic<qint64> m_stopAtNs{0};
    mutable std::mutex m_mutex;
    RecordingStatus m_status;
};

RecordingController::RecordingController(Compositor &video, AudioMixer &audio, QObject *parent)
    : QObject(parent), m_video(video), m_audio(audio)
{
    m_statusPollTimer.setInterval(50);
    connect(&m_statusPollTimer, &QTimer::timeout, this, &RecordingController::refreshWorkerStatus);
    m_statusPollTimer.start();
    for (auto *thread : {static_cast<QThread *>(&m_video), static_cast<QThread *>(&m_audio)}) {
        connect(thread, &QThread::started, this, &RecordingController::availabilityChanged);
        connect(thread, &QThread::finished, this, &RecordingController::availabilityChanged);
    }
}

bool RecordingController::ready() const
{
    return m_video.isRunning() && m_audio.isRunning();
}

RecordingController::~RecordingController()
{
    m_statusPollTimer.stop();
    if (m_worker) {
        m_worker->requestStop();
        m_worker->wait();
    }
}

bool RecordingController::startRecording(const QString &path, int bitrateKbps)
{
    if (busy() || (m_worker && m_worker->isRunning()))
        return false;
    // A completed session must not overwrite a subsequent validation error
    // when the periodic status poll runs.
    m_worker.reset();
    if (!m_video.isRunning() || !m_audio.isRunning() || !QFileInfo(path).isAbsolute() ||
        QFileInfo(path).suffix().compare("mp4", Qt::CaseInsensitive) || bitrateKbps < 500 ||
        bitrateKbps > 50000) {
        m_status = {};
        m_status.state = "error";
        m_status.error = tr("合成或混音尚未就绪，或录制路径/码率无效。");
        emit statusChanged();
        return false;
    }
    m_worker = std::make_unique<RecordingWorker>(m_video, m_audio, path, bitrateKbps);
    connect(m_worker.get(), &QThread::finished, this, &RecordingController::refreshWorkerStatus,
            Qt::QueuedConnection);
    m_status = m_worker->snapshot();
    emit statusChanged();
    m_worker->start(QThread::HighPriority);
    return true;
}

void RecordingController::stopRecording()
{
    if (!busy() || !m_worker)
        return;
    m_worker->requestStop();
    m_status.state = "stopping";
    emit statusChanged();
}

void RecordingController::refreshWorkerStatus()
{
    if (!m_worker)
        return;
    const bool wasBusy = busy();
    const bool stopping = state() == "stopping";
    m_status = m_worker->snapshot();
    // Completion is observable only after output taps and worker-owned resources close.
    if (!m_worker->isFinished() &&
        (stopping || m_status.state == "idle" || m_status.state == "error"))
        m_status.state = "stopping";
    if (m_status.pressured && busy())
        emit overloaded();
    emit statusChanged();
    if (wasBusy && !busy())
        emit finished();
}
