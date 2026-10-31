#ifndef OUTPUTSETTINGS_H
#define OUTPUTSETTINGS_H

#include <QObject>

class OutputSettings : public QObject
{
    Q_OBJECT

public:
    explicit OutputSettings(QObject *parent = nullptr);

    QString recordingDirectory() const
    {
        return m_recordingDirectory;
    }

    int recordingBitrate() const
    {
        return m_recordingBitrate;
    }

    int streamingBitrate() const
    {
        return m_streamingBitrate;
    }

    QString streamServer() const
    {
        return m_streamServer;
    }

    QString streamKey() const
    {
        return m_streamKey;
    }

    bool recordingLocked() const
    {
        return m_recordingLocked;
    }

    bool streamingLocked() const
    {
        return m_streamingLocked;
    }

    QString lastError() const
    {
        return m_lastError;
    }

    void setRecordingLocked(bool locked);
    void setStreamingLocked(bool locked);
    static QString validateStream(const QString &server, const QString &key, int bitrateKbps,
                                  bool allowEmpty = true);
    bool apply(const QString &directory, int recordingBitrate, const QString &server,
               const QString &key, int streamingBitrate = 6000);
    QString nextRecordingPath(); // Creates the selected directory only at recording start.

signals:
    void recordingLockedChanged();
    void streamingLockedChanged();

private:
    bool fail(const QString &message);

    QString m_recordingDirectory;
    QString m_streamServer;
    QString m_streamKey;
    QString m_lastError;

    int m_recordingBitrate = 12000;
    int m_streamingBitrate = 6000;
    bool m_recordingLocked = false;
    bool m_streamingLocked = false;
};
#endif
