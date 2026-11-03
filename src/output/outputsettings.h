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

    int recordingBitrateKbps() const
    {
        return m_recordingBitrateKbps;
    }

    int streamingBitrateKbps() const
    {
        return m_streamingBitrateKbps;
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
    bool apply(const QString &directory, int recordingBitrateKbps, const QString &server,
               const QString &key, int streamingBitrateKbps = 6000);
    QString nextRecordingPath(); // 开始录制时才创建目录，避免仅编辑设置就修改文件系统。

signals:
    void recordingLockedChanged();
    void streamingLockedChanged();

private:
    bool fail(const QString &message);

    QString m_recordingDirectory;
    QString m_streamServer;
    QString m_streamKey;
    QString m_lastError;

    int m_recordingBitrateKbps = 12000;
    int m_streamingBitrateKbps = 6000;
    bool m_recordingLocked = false;
    bool m_streamingLocked = false;
};
#endif
