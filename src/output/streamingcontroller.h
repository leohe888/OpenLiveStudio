#ifndef STREAMINGCONTROLLER_H
#define STREAMINGCONTROLLER_H
#include <QObject>
#include <QTimer>
#include <memory>
class Compositor;
class AudioMixer;
class StreamingWorker;

struct StreamingStatus
{
    QString state = QStringLiteral("idle"), error;
    qint64 durationMs = 0, bitrateKbps = 0;
    quint64 droppedFrames = 0, reconnects = 0;
    int retryInSeconds = 0;
    qint64 lastEncodeMs = 0, lagMs = 0;
};

class StreamingController : public QObject
{
    Q_OBJECT
public:
    StreamingController(Compositor &video, AudioMixer &audio, QObject *parent = nullptr);
    ~StreamingController() override;

    // State name only ("idle", "connecting", "live", "reconnecting", "stopping", "error");
    // status() returns the whole snapshot including duration, bitrate and counters.
    QString state() const
    {
        return m_status.state;
    }

    bool busy() const
    {
        return state() == "connecting" || state() == "live" || state() == "reconnecting" ||
               state() == "stopping";
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
