#ifndef STREAMINGCONTROLLER_H
#define STREAMINGCONTROLLER_H
#include <QObject>
#include <QTimer>
#include <memory>
class Compositor;
class AudioMixer;
class StreamingWorker;

enum class StreamingState {
    Idle,
    Connecting,
    Live,
    Reconnecting,
    Stopping,
    Error,
};

// Stable diagnostic names; UI text is translated separately.
inline QString outputStateName(StreamingState state)
{
    switch (state) {
    case StreamingState::Idle:
        return QStringLiteral("idle");
    case StreamingState::Connecting:
        return QStringLiteral("connecting");
    case StreamingState::Live:
        return QStringLiteral("live");
    case StreamingState::Reconnecting:
        return QStringLiteral("reconnecting");
    case StreamingState::Stopping:
        return QStringLiteral("stopping");
    case StreamingState::Error:
        return QStringLiteral("error");
    }
    Q_UNREACHABLE();
}

struct StreamingStatus
{
    StreamingState state = StreamingState::Idle;
    QString error;
    qint64 durationMs = 0, bitrateKbps = 0;
    quint64 droppedFrames = 0, reconnects = 0;
    int retryInSeconds = 0;
    qint64 lastEncodeMs = 0, lagMs = 0;
};

// 控制与状态接口只在所属（界面）线程调用；内部 worker 发布受锁保护的状态副本。
// video/audio 必须比控制器存活更久；析构取消网络操作并等待 worker 退出。
class StreamingController : public QObject
{
    Q_OBJECT
public:
    StreamingController(Compositor &video, AudioMixer &audio, QObject *parent = nullptr);
    ~StreamingController() override;

    StreamingState state() const
    {
        return m_status.state;
    }

    bool busy() const
    {
        return state() == StreamingState::Connecting || state() == StreamingState::Live ||
               state() == StreamingState::Reconnecting || state() == StreamingState::Stopping;
    }

    bool ready() const;

    QString lastError() const
    {
        return m_status.error;
    }

    StreamingStatus status() const
    {
        return m_status;
    }

    bool startStreaming(const QString &server, const QString &key, int bitrateKbps = 6000);
    void stopStreaming();
    void stopForOverload();
signals:
    void statusChanged();
    void availabilityChanged();
    void finished();

private:
    void refreshWorkerStatus();
    Compositor &m_video;
    AudioMixer &m_audio;
    std::unique_ptr<StreamingWorker> m_worker;
    QTimer m_statusPollTimer;
    StreamingStatus m_status;
};
#endif
