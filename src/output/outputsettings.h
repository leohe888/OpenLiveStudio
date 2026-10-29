#ifndef OUTPUTSETTINGS_H
#define OUTPUTSETTINGS_H
#include <QObject>
class OutputSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordingDirectory READ recordingDirectory NOTIFY settingsChanged)
    Q_PROPERTY(int recordingBitrate READ recordingBitrate NOTIFY settingsChanged)
    Q_PROPERTY(int streamingBitrate READ streamingBitrate NOTIFY settingsChanged)
    Q_PROPERTY(QString streamServer READ streamServer NOTIFY settingsChanged)
    Q_PROPERTY(QString streamKey READ streamKey NOTIFY settingsChanged)
    Q_PROPERTY(bool recordingLocked READ recordingLocked NOTIFY recordingLockedChanged)
    Q_PROPERTY(bool streamingLocked READ streamingLocked NOTIFY streamingLockedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit OutputSettings(QObject *parent = nullptr);
    QString recordingDirectory() const { return m_directory; }
    int recordingBitrate() const { return m_recordingBitrate; }
    int streamingBitrate() const { return m_streamingBitrate; }
    QString streamServer() const { return m_server; }
    QString streamKey() const { return m_key; }
    bool recordingLocked() const { return m_locked; }
    bool streamingLocked() const { return m_streamingLocked; }
    QString lastError() const { return m_error; }
    void setRecordingLocked(bool locked);
    void setStreamingLocked(bool locked);
    static QString validateStream(const QString &server,const QString &key,int bitrateKbps,bool allowEmpty=true);
    Q_INVOKABLE bool apply(const QString &directory,int recordingBitrate,const QString &server,const QString &key,int streamingBitrate=6000);
    QString nextRecordingPath(); // Creates the selected directory only at recording start.
signals:
    void settingsChanged();
    void recordingLockedChanged();
    void streamingLockedChanged();
    void lastErrorChanged();
private:
    bool fail(const QString &message);
    QString m_directory, m_server, m_key, m_error;
    int m_recordingBitrate = 12000,m_streamingBitrate=6000;
    bool m_locked = false,m_streamingLocked=false;
};
#endif
