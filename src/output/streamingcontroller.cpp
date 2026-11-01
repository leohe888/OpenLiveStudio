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
    StreamingWorker(Compositor &video, AudioMixer &audio, QString server, QString key, int bitrate)
        : m_video(video), m_audio(audio), m_server(std::move(server)), m_key(std::move(key)),
          m_bitrate(bitrate)
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
        qint64 firstConnected = 0;
        int attempt = 0;
        try {
            while (!m_stop) {
                try {
                    status.state = attempt ? "reconnecting" : "connecting";
                    status.retryInSeconds = 0;
                    status.bitrateKbps = 0;
                    publish(status, firstConnected);
                    const QString endpoint = resolvedEndpoint();
                    if (m_stop)
                        break;
                    MediaEncoder encoder;
                    encoder.openStream(
                        endpoint, m_bitrate,
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
                    if (!firstConnected)
                        firstConnected = MediaClock::nowNs();
                    status.state = "live";
                    status.error.clear();
                    publish(status, firstConnected);
                    qint64 frame = 0, rateTime = MediaClock::nowNs(),
                           rateBytes = encoder.bytesSent();
                    qint64 lastEncodeNs = 0, lastNetworkStall = 0, processingTotal = 0,
                           slowSince = 0;
                    std::deque<std::pair<qint64, qint64>> processing;
                    qint64 skipped = 0;
                    const auto performanceStart = MediaClock::nowNs();
                    const int pauseOnce = std::clamp(
                        qEnvironmentVariableIntValue("OPENLIVE_STREAMING_PAUSE_ONCE_MS"), 0, 1500);
                    const auto previousDropped = status.droppedFrames;
                    const int delay = std::clamp(
                        qEnvironmentVariableIntValue("OPENLIVE_STREAMING_ENCODER_DELAY_MS"), 0,
                        1000);
                    while (!m_stop) {
                        status.lagMs = timeline.lag(frame) / 1000000;
                        // Live output prioritizes freshness. A scheduling hitch
                        // skips media time instead of encoding a catch-up burst.
                        // Network backlog requires a fresh socket as well.
                        if (timeline.lag(frame) > 1000000000LL) {
                            if (lastNetworkStall &&
                                MediaClock::nowNs() - lastNetworkStall < 3000000000LL)
                                throw StreamFailure{
                                    QStringLiteral("直播发送积压，正在恢复实时画面。"), true};
                            const auto fresh = std::max(
                                frame + 1, (MediaClock::nowNs() - timeline.origin() - 200000000LL) *
                                               30 / 1000000000LL);
                            skipped += fresh - frame;
                            frame = fresh;
                            encoder.skipTo(frame);
                            timeline.recover();
                            status.droppedFrames =
                                previousDropped + quint64(skipped) + timeline.dropped();
                        }
                        QImage image;
                        QVector<float> audio;
                        if (!timeline.read(frame, image, audio)) {
                            publish(status, firstConnected);
                            msleep(3);
                            continue;
                        }
                        const auto encodeStart = MediaClock::nowNs(),
                                   networkStart = encoder.outputTimeNs();
                        encoder.video(image, frame);
                        encoder.audio(audio);
                        if (delay)
                            msleep(unsigned(delay));
                        if (pauseOnce && frame == 60)
                            msleep(unsigned(pauseOnce));
                        lastEncodeNs = MediaClock::nowNs() - encodeStart -
                                       (encoder.outputTimeNs() - networkStart);
                        status.lastEncodeMs = lastEncodeNs / 1000000;
                        const auto processedAt = MediaClock::nowNs();
                        if (encoder.outputTimeNs() - networkStart > 100000000LL)
                            lastNetworkStall = processedAt;
                        processing.emplace_back(processedAt, lastEncodeNs);
                        processingTotal += lastEncodeNs;
                        while (!processing.empty() &&
                               processing.front().first < processedAt - 10000000000LL) {
                            processingTotal -= processing.front().second;
                            processing.pop_front();
                        }
                        if (processedAt - performanceStart > 3000000000LL &&
                            processing.size() >= 15 &&
                            processingTotal / qint64(processing.size()) > 33333333LL) {
                            if (!slowSince)
                                slowSince = processedAt;
                            if (processedAt - slowSince > 2000000000LL)
                                throw QStringLiteral(
                                    "推流编码持续跟不上实时速度，直播已停止；请减少系统负载。");
                        } else
                            slowSince = 0;
                        ++frame;
                        status.droppedFrames =
                            previousDropped + quint64(skipped) + timeline.dropped();
                        if (frame >= 60)
                            attempt = 0; // Backoff is for consecutive failures.
                        const auto now = MediaClock::nowNs();
                        if (now - rateTime >= 1000000000LL) {
                            status.bitrateKbps =
                                (encoder.bytesSent() - rateBytes) * 8000000LL / (now - rateTime);
                            rateTime = now;
                            rateBytes = encoder.bytesSent();
                        }
                        publish(status, firstConnected);
                    }
                    // On user stop, interrupt network cleanup immediately. RTMP
                    // has no file trailer to preserve; do not flush stale frames.
                } catch (const StreamFailure &failure) {
                    if (m_stop)
                        break;
                    if (!failure.retryable)
                        throw failure.message;
                    status.error = failure.message;
                    status.state = "reconnecting";
                    status.bitrateKbps = 0;
                    const int waits[]{1, 2, 5, 10};
                    const int seconds = waits[std::min(attempt, 3)];
                    ++attempt;
                    ++status.reconnects;
                    const auto until = MediaClock::nowNs() + qint64(seconds) * 1000000000LL;
                    while (!m_stop && MediaClock::nowNs() < until) {
                        status.retryInSeconds =
                            int((until - MediaClock::nowNs() + 999999999LL) / 1000000000LL);
                        publish(status, firstConnected);
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
        publish(status, firstConnected);
    }

private:
    QString resolvedEndpoint()
    {
        QUrl url(m_server + "/" + m_key, QUrl::StrictMode);
        QHostAddress address;
        if (!address.setAddress(url.host())) {
            // Resolve asynchronously so native getaddrinfo cannot hold this
            // output's cancellation or application close hostage.
            QEventLoop loop;
            QTimer timer;
            timer.setInterval(25);
            const auto deadline = MediaClock::nowNs() + 3000000000LL;
            QList<QHostAddress> addresses;
            bool complete = false;
            const int lookup = QHostInfo::lookupHost(url.host(), &loop, [&](const QHostInfo &info) {
                addresses = info.addresses();
                complete = true;
                loop.quit();
            });
            QObject::connect(&timer, &QTimer::timeout, &loop, [&] {
                if (m_stop || MediaClock::nowNs() > deadline)
                    loop.quit();
            });
            timer.start();
            loop.exec();
            QHostInfo::abortHostLookup(lookup);
            if (m_stop)
                return {};
            if (!complete || addresses.empty())
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

    void publish(StreamingStatus &status, qint64 origin)
    {
        if (origin)
            status.durationMs = (MediaClock::nowNs() - origin) / 1000000;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_status = status;
    }

    Compositor &m_video;
    AudioMixer &m_audio;
    QString m_server, m_key;
    int m_bitrate;
    std::atomic<bool> m_stop{false}, m_overload{false};
    mutable std::mutex m_mutex;
    StreamingStatus m_status;
};

StreamingController::StreamingController(Compositor &video, AudioMixer &audio, QObject *parent)
    : QObject(parent), m_video(video), m_audio(audio)
{
    m_poll.setInterval(50);
    connect(&m_poll, &QTimer::timeout, this, &StreamingController::poll);
    m_poll.start();
    for (auto *thread : {static_cast<QThread *>(&video), static_cast<QThread *>(&audio)}) {
        connect(thread, &QThread::started, this, &StreamingController::availabilityChanged);
        connect(thread, &QThread::finished, this, &StreamingController::availabilityChanged);
    }
}

StreamingController::~StreamingController()
{
    m_poll.stop();
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
    connect(m_worker.get(), &QThread::finished, this, &StreamingController::poll,
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

void StreamingController::poll()
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
