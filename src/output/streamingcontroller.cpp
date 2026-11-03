#include "output/streamingcontroller.h"
#include "output/outputsettings.h"
#include "output/outputtimeline.h"
#include "output/mediaencoder.h"
#include <QHostInfo>
#include <QHostAddress>
#include <QEventLoop>
#include <QUrl>
#include <atomic>

class StreamingWorker : public QThread
{
public:
    StreamingWorker(Compositor &video, AudioMixer &audio, QString server, QString key,
                    int bitrateKbps)
        : m_video(video), m_audio(audio), m_server(std::move(server)), m_key(std::move(key)),
          m_bitrateKbps(bitrateKbps)
    {
        m_status.state = "connecting";
    }

    ~StreamingWorker() override
    {
        stop();
        wait();
    }

    void stop(bool overload = false)
    {
        if (overload)
            m_overload = true;
        m_stop = true;
    }

    StreamingStatus snapshot() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_status;
    }

protected:
    void run() override
    {
        StreamingStatus status = snapshot();
        qint64 firstConnectedNs = 0;
        int attempt = 0;
        try {
            while (!m_stop) {
                try {
                    status.state = attempt ? "reconnecting" : "connecting";
                    status.retryInSeconds = 0;
                    status.bitrateKbps = 0;
                    publish(status, firstConnectedNs);
                    const QString endpoint = resolvedEndpoint();
                    if (m_stop)
                        break;
                    MediaEncoder encoder;
                    encoder.openStream(
                        endpoint, m_bitrateKbps,
                        [&] {
                            return m_stop.load();
                        },
                        m_server);
                    if (m_stop)
                        break;
                    OutputTimeline timeline(m_video, m_audio);
                    if (!timeline.initialize([&] {
                            return m_stop.load();
                        }))
                        break;
                    if (!firstConnectedNs)
                        firstConnectedNs = MediaClock::nowNs();
                    status.state = "live";
                    status.error.clear();
                    publish(status, firstConnectedNs);
                    qint64 frameIndex = 0, bitrateWindowStartNs = MediaClock::nowNs(),
                           bitrateWindowStartBytes = encoder.bytesSent();
                    qint64 lastEncodeNs = 0, lastNetworkStallNs = 0, processingTotalNs = 0,
                           slowSinceNs = 0;
                    std::deque<std::pair<qint64, qint64>> recentEncodeDurations;
                    qint64 skippedVideoFrames = 0;
                    const auto performanceStartNs = MediaClock::nowNs();
                    const int artificialPauseOnceMs = std::clamp(
                        qEnvironmentVariableIntValue("OPENLIVE_STREAMING_PAUSE_ONCE_MS"), 0, 1500);
                    const auto previousDropped = status.droppedFrames;
                    const int artificialEncodeDelayMs = std::clamp(
                        qEnvironmentVariableIntValue("OPENLIVE_STREAMING_ENCODER_DELAY_MS"), 0,
                        1000);
                    while (!m_stop) {
                        status.lagMs = timeline.lagNs(frameIndex) / 1000000;
                        // 直播优先保持实时性，落后时跳过旧内容，避免集中补帧；若网络也发生积压，则重建连接。
                        if (timeline.lagNs(frameIndex) > 1000000000LL) {
                            if (lastNetworkStallNs &&
                                MediaClock::nowNs() - lastNetworkStallNs < 3000000000LL)
                                throw StreamFailure{
                                    QStringLiteral("直播发送积压，正在恢复实时画面。"), true};
                            const auto freshFrameIndex =
                                std::max(frameIndex + 1,
                                         (MediaClock::nowNs() - timeline.originNs() - 200000000LL) *
                                             30 / 1000000000LL);
                            skippedVideoFrames += freshFrameIndex - frameIndex;
                            frameIndex = freshFrameIndex;
                            encoder.skipTo(frameIndex);
                            timeline.recover();
                            status.droppedFrames = previousDropped + quint64(skippedVideoFrames) +
                                                   timeline.droppedVideoFrames();
                        }
                        QImage image;
                        QVector<float> audio;
                        if (!timeline.read(frameIndex, image, audio)) {
                            publish(status, firstConnectedNs);
                            msleep(3);
                            continue;
                        }
                        const auto encodeStartNs = MediaClock::nowNs(),
                                   outputTimeBeforeNs = encoder.outputTimeNs();
                        encoder.encodeVideoFrame(image, frameIndex);
                        encoder.encodeAudioSamples(audio);
                        if (artificialEncodeDelayMs)
                            msleep(unsigned(artificialEncodeDelayMs));
                        if (artificialPauseOnceMs && frameIndex == 60)
                            msleep(unsigned(artificialPauseOnceMs));
                        lastEncodeNs = MediaClock::nowNs() - encodeStartNs -
                                       (encoder.outputTimeNs() - outputTimeBeforeNs);
                        status.lastEncodeMs = lastEncodeNs / 1000000;
                        const auto processedAtNs = MediaClock::nowNs();
                        if (encoder.outputTimeNs() - outputTimeBeforeNs > 100000000LL)
                            lastNetworkStallNs = processedAtNs;
                        recentEncodeDurations.emplace_back(processedAtNs, lastEncodeNs);
                        processingTotalNs += lastEncodeNs;
                        while (!recentEncodeDurations.empty() &&
                               recentEncodeDurations.front().first <
                                   processedAtNs - 10000000000LL) {
                            processingTotalNs -= recentEncodeDurations.front().second;
                            recentEncodeDurations.pop_front();
                        }
                        if (processedAtNs - performanceStartNs > 3000000000LL &&
                            recentEncodeDurations.size() >= 15 &&
                            processingTotalNs / qint64(recentEncodeDurations.size()) > 33333333LL) {
                            if (!slowSinceNs)
                                slowSinceNs = processedAtNs;
                            if (processedAtNs - slowSinceNs > 2000000000LL)
                                throw QStringLiteral(
                                    "推流编码持续跟不上实时速度，直播已停止；请减少系统负载。");
                        } else
                            slowSinceNs = 0;
                        ++frameIndex;
                        status.droppedFrames = previousDropped + quint64(skippedVideoFrames) +
                                               timeline.droppedVideoFrames();
                        if (frameIndex >= 60)
                            attempt = 0; // 稳定出帧后清除连续失败计数，避免后续独立故障继承过长退避。
                        const auto now = MediaClock::nowNs();
                        if (now - bitrateWindowStartNs >= 1000000000LL) {
                            status.bitrateKbps = (encoder.bytesSent() - bitrateWindowStartBytes) *
                                                 8000000LL / (now - bitrateWindowStartNs);
                            bitrateWindowStartNs = now;
                            bitrateWindowStartBytes = encoder.bytesSent();
                        }
                        publish(status, firstConnectedNs);
                    }
                    // 用户停止直播时立即中断网络清理；无需像本地文件那样完成封装，也不再发送积压帧。
                } catch (const StreamFailure &failure) {
                    if (m_stop)
                        break;
                    if (!failure.retryable)
                        throw failure.message;
                    status.error = failure.message;
                    status.state = "reconnecting";
                    status.bitrateKbps = 0;
                    const int retryDelaysSeconds[]{1, 2, 5, 10};
                    const int retryDelaySeconds = retryDelaysSeconds[std::min(attempt, 3)];
                    ++attempt;
                    ++status.reconnects;
                    const auto retryDeadlineNs =
                        MediaClock::nowNs() + qint64(retryDelaySeconds) * 1000000000LL;
                    while (!m_stop && MediaClock::nowNs() < retryDeadlineNs) {
                        status.retryInSeconds = int(
                            (retryDeadlineNs - MediaClock::nowNs() + 999999999LL) / 1000000000LL);
                        publish(status, firstConnectedNs);
                        msleep(25);
                    }
                }
            }
            status.state = "idle";
            status.bitrateKbps = 0;
            status.retryInSeconds = 0;
            if (m_overload) {
                status.state = "error";
                status.error = QStringLiteral("系统输出过载，已优先停止直播以保留录制资源。");
            }
        } catch (const QString &error) {
            status.state = "error";
            status.error = error;
        } catch (const std::exception &) {
            status.state = "error";
            status.error = QStringLiteral("直播输出发生内部错误。");
        }
        if (m_stop && !m_overload) {
            status.state = "idle";
            status.error.clear();
        }
        status.bitrateKbps = 0;
        status.retryInSeconds = 0;
        publish(status, firstConnectedNs);
    }

private:
    QString resolvedEndpoint()
    {
        QUrl url(m_server + "/" + m_key, QUrl::StrictMode);
        QHostAddress address;
        if (!address.setAddress(url.host())) {
            // 异步解析域名，避免系统 getaddrinfo 阻塞时无法取消输出或关闭应用。
            QEventLoop loop;
            QTimer timer;
            timer.setInterval(25);
            const auto lookupDeadlineNs = MediaClock::nowNs() + 3000000000LL;
            QList<QHostAddress> addresses;
            bool lookupCompleted = false;
            const int lookupId =
                QHostInfo::lookupHost(url.host(), &loop, [&](const QHostInfo &info) {
                    addresses = info.addresses();
                    lookupCompleted = true;
                    loop.quit();
                });
            QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
                if (m_stop || MediaClock::nowNs() > lookupDeadlineNs)
                    loop.quit();
            });
            timer.start();
            loop.exec();
            QHostInfo::abortHostLookup(lookupId);
            if (m_stop)
                return {};
            if (!lookupCompleted || addresses.empty())
                throw StreamFailure{QStringLiteral("无法解析推流服务器地址。"), true};
            address = addresses.front();
            for (const auto &candidate : addresses)
                if (candidate.protocol() == QAbstractSocket::IPv4Protocol) {
                    address = candidate;
                    break;
                }
            url.setHost(address.toString());
        }
        return url.toString(QUrl::FullyEncoded);
    }

    void publish(StreamingStatus &status, qint64 originNs)
    {
        if (originNs)
            status.durationMs = (MediaClock::nowNs() - originNs) / 1000000;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = status;
    }

    Compositor &m_video;
    AudioMixer &m_audio;
    QString m_server, m_key;
    int m_bitrateKbps;
    std::atomic<bool> m_stop{false}, m_overload{false};
    mutable std::mutex m_mutex;
    StreamingStatus m_status;
};

StreamingController::StreamingController(Compositor &video, AudioMixer &audio, QObject *parent)
    : QObject(parent), m_video(video), m_audio(audio)
{
    m_statusPollTimer.setInterval(50);
    connect(&m_statusPollTimer, &QTimer::timeout, this, &StreamingController::refreshWorkerStatus);
    m_statusPollTimer.start();
    for (auto *thread : {static_cast<QThread *>(&video), static_cast<QThread *>(&audio)}) {
        connect(thread, &QThread::started, this, &StreamingController::availabilityChanged);
        connect(thread, &QThread::finished, this, &StreamingController::availabilityChanged);
    }
}

StreamingController::~StreamingController()
{
    m_statusPollTimer.stop();
    if (m_worker) {
        m_worker->stop();
        m_worker->wait();
    }
}

bool StreamingController::ready() const
{
    return m_video.isRunning() && m_audio.isRunning();
}

bool StreamingController::startStreaming(const QString &server, const QString &key, int bitrateKbps)
{
    if (busy() || (m_worker && m_worker->isRunning()))
        return false;
    m_worker.reset();
    const auto error = OutputSettings::validateStream(server, key, bitrateKbps, false);
    if (!ready() || !error.isEmpty()) {
        m_status = {};
        m_status.state = "error";
        m_status.error = error.isEmpty() ? tr("合成或混音尚未就绪。") : error;
        emit statusChanged();
        return false;
    }
    QString endpoint = server.trimmed();
    while (endpoint.endsWith('/'))
        endpoint.chop(1);
    m_worker = std::make_unique<StreamingWorker>(m_video, m_audio, endpoint, key, bitrateKbps);
    connect(m_worker.get(), &QThread::finished, this, &StreamingController::refreshWorkerStatus,
            Qt::QueuedConnection);
    m_status = m_worker->snapshot();
    emit statusChanged();
    m_worker->start();
    return true;
}

void StreamingController::stopStreaming()
{
    if (busy() && m_worker) {
        m_worker->stop();
        m_status.state = "stopping";
        emit statusChanged();
    }
}

void StreamingController::stopForOverload()
{
    if (busy() && m_worker) {
        m_worker->stop(true);
        m_status.state = "stopping";
        m_status.error = tr("系统输出过载，正在优先停止直播。");
        emit statusChanged();
    }
}

void StreamingController::refreshWorkerStatus()
{
    if (!m_worker)
        return;
    const bool wasBusy = busy();
    const bool stopping = state() == "stopping";
    m_status = m_worker->snapshot();
    if (!m_worker->isFinished() && (stopping || state() == "idle" || state() == "error"))
        m_status.state = "stopping";
    emit statusChanged();
    if (wasBusy && !busy())
        emit finished();
}
