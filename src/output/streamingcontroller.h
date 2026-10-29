#ifndef STREAMINGCONTROLLER_H
#define STREAMINGCONTROLLER_H
#include <QObject>
#include <QTimer>
#include <memory>
class Compositor;
class AudioMixer;
class StreamingWorker;
struct StreamingStatus {
    QString state=QStringLiteral("idle"),error;
    qint64 durationMs=0,bitrateKbps=0;
    quint64 droppedFrames=0,reconnects=0;
    int retryInSeconds=0;
    qint64 lastEncodeMs=0,lagMs=0;
};
class StreamingController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY availabilityChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
public:
    StreamingController(Compositor &video,AudioMixer &audio,QObject *parent=nullptr);
    ~StreamingController() override;
    QString state() const { return m_status.state; }
    bool busy() const { return state()=="connecting" || state()=="live" || state()=="reconnecting" || state()=="stopping"; }
    bool ready() const;
    QString lastError() const { return m_status.error; }
    StreamingStatus status() const { return m_status; }
    Q_INVOKABLE bool startStreaming(const QString &server,const QString &key,int bitrateKbps=6000);
    Q_INVOKABLE void stopStreaming();
    void stopForOverload();
signals:
    void statusChanged();
    void availabilityChanged();
    void finished();
private:
    void poll();
    Compositor &m_video; AudioMixer &m_audio;
    std::unique_ptr<StreamingWorker> m_worker;
    QTimer m_poll;
    StreamingStatus m_status;
};
#endif
