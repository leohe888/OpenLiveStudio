#ifndef RECORDINGCONTROLLER_H
#define RECORDINGCONTROLLER_H
#include <QObject>
#include <QTimer>
#include <memory>
class Compositor;
class AudioMixer;
class RecordingWorker;

enum class RecordingState {
    Idle,
    Starting,
    Recording,
    Stopping,
    Error,
};

// Stable diagnostic names; UI text is translated separately.
inline QString outputStateName(RecordingState state)
{
    switch (state) {
    case RecordingState::Idle:
        return QStringLiteral("idle");
    case RecordingState::Starting:
        return QStringLiteral("starting");
    case RecordingState::Recording:
        return QStringLiteral("recording");
    case RecordingState::Stopping:
        return QStringLiteral("stopping");
    case RecordingState::Error:
        return QStringLiteral("error");
    }
    Q_UNREACHABLE();
}

struct RecordingStatus
{
    RecordingState state = RecordingState::Idle;
    QString path, error;
    qint64 durationMs = 0, videoFrames = 0, missingAudioFrames = 0;
    qint64 lagMs = 0, lastEncodeMs = 0, lastOutputMs = 0;
    qint64 videoEncodeMs = 0, audioEncodeMs = 0, conversionMs = 0;
    int imageFormat = 0;
    quint64 repeatedFrames = 0, droppedFrames = 0;
    bool fileComplete = false, pressured = false;
};

// 控制与状态接口只在所属（界面）线程调用；内部 worker 发布受锁保护的状态副本。
// video/audio 必须比控制器存活更久；析构请求停止并等待文件封装与资源释放。
class RecordingController : public QObject
{
    Q_OBJECT
public:
    RecordingController(Compositor &video, AudioMixer &audio, QObject *parent = nullptr);
    ~RecordingController() override;

    RecordingState state() const
    {
        return m_status.state;
    }

    bool busy() const
    {
        return state() == RecordingState::Starting || state() == RecordingState::Recording ||
               state() == RecordingState::Stopping;
    }

    bool ready() const;

    QString filePath() const
    {
        return m_status.path;
    }

    QString lastError() const
    {
        return m_status.error;
    }

    bool fileComplete() const
    {
        return m_status.fileComplete;
    }

    qint64 durationMs() const
    {
        return m_status.durationMs;
    }

    quint64 repeatedFrames() const
    {
        return m_status.repeatedFrames;
    }

    quint64 droppedFrames() const
    {
        return m_status.droppedFrames;
    }

    RecordingStatus status() const
    {
        return m_status;
    }

    bool startRecording(const QString &path, int bitrateKbps = 12000);
    void stopRecording();
signals:
    void statusChanged();
    void overloaded();
    void finished();
    void availabilityChanged();

private:
    void refreshWorkerStatus();
    Compositor &m_video;
    AudioMixer &m_audio;
    std::unique_ptr<RecordingWorker> m_worker;
    QTimer m_statusPollTimer;
    RecordingStatus m_status;
};
#endif
