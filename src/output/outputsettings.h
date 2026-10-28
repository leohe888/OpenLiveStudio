#ifndef OUTPUTSETTINGS_H
#define OUTPUTSETTINGS_H
#include <QObject>
class OutputSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString recordingDirectory READ recordingDirectory NOTIFY settingsChanged)
    Q_PROPERTY(int quality READ quality NOTIFY settingsChanged)
    Q_PROPERTY(QString streamServer READ streamServer NOTIFY settingsChanged)
    Q_PROPERTY(QString streamKey READ streamKey NOTIFY settingsChanged)
    Q_PROPERTY(bool recordingLocked READ recordingLocked NOTIFY recordingLockedChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    explicit OutputSettings(QObject *parent = nullptr);
    QString recordingDirectory() const { return m_directory; }
    int quality() const { return m_quality; }
    QString streamServer() const { return m_server; }
    QString streamKey() const { return m_key; }
    bool recordingLocked() const { return m_locked; }
    QString lastError() const { return m_error; }
    void setRecordingLocked(bool locked);
    Q_INVOKABLE bool apply(const QString &directory,int quality,const QString &server,const QString &key);
    QString nextRecordingPath(); // Creates the selected directory only at recording start.
signals:
    void settingsChanged();
    void recordingLockedChanged();
    void lastErrorChanged();
private:
    bool fail(const QString &message);
    QString m_directory, m_server, m_key, m_error;
    int m_quality = 1;
    bool m_locked = false;
};
#endif
