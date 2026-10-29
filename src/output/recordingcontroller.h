#ifndef RECORDINGCONTROLLER_H
#define RECORDINGCONTROLLER_H
#include <QObject>
#include <QTimer>
#include <memory>
class Compositor;
class AudioMixer;
class RecordingWorker;

struct RecordingStatus {
    QString state = QStringLiteral("idle"), path, error;
    qint64 durationMs = 0, videoFrames = 0, missingAudioSamples = 0;
    qint64 lagMs=0,lastEncodeMs=0,lastOutputMs=0;
    qint64 videoEncodeMs=0,audioEncodeMs=0,conversionMs=0;
    int imageFormat=0;
    quint64 repeatedFrames = 0, droppedFrames = 0;
    bool fileComplete = false, pressured = false;
};

class RecordingController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY availabilityChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY statusChanged)
    Q_PROPERTY(bool fileComplete READ fileComplete NOTIFY statusChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY statusChanged)
    Q_PROPERTY(quint64 repeatedFrames READ repeatedFrames NOTIFY statusChanged)
    Q_PROPERTY(quint64 droppedFrames READ droppedFrames NOTIFY statusChanged)
public:
    RecordingController(Compositor &video, AudioMixer &audio, QObject *parent = nullptr);
    ~RecordingController() override;
    QString state() const { return m_status.state; }
    bool busy() const { return state()=="starting" || state()=="recording" || state()=="stopping"; }
    bool ready() const;
    QString filePath() const { return m_status.path; }
    QString lastError() const { return m_status.error; }
    bool fileComplete() const { return m_status.fileComplete; }
    qint64 durationMs() const { return m_status.durationMs; }
    quint64 repeatedFrames() const { return m_status.repeatedFrames; }
    quint64 droppedFrames() const { return m_status.droppedFrames; }
    RecordingStatus status() const { return m_status; }
    Q_INVOKABLE bool startRecording(const QString &path, int bitrateKbps = 12000);
    Q_INVOKABLE void stopRecording();
signals:
    void statusChanged();
    void overloaded();
    void finished();
    void availabilityChanged();
private:
    void poll();
    Compositor &m_video;
    AudioMixer &m_audio;
    std::unique_ptr<RecordingWorker> m_worker;
    QTimer m_poll;
    RecordingStatus m_status;
};
#endif
