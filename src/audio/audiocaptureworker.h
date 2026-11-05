#ifndef AUDIOCAPTUREWORKER_H
#define AUDIOCAPTUREWORKER_H
#include "audio/audiobuffer.h"
#include "audio/audiosource.h"
#include <QThread>
#include <QList>
#include <mutex>

struct AudioDevice
{
    QString id;
    QString name;
    AudioSourceType type = AudioSourceType::Microphone;
};

QString defaultAudioDeviceId(AudioSourceType type);
QList<AudioDevice> audioDevices(QString *error);

class AudioCaptureWorker : public QThread
{
public:
    AudioCaptureWorker(QString deviceId, AudioSourceType type,
                       std::shared_ptr<AudioPacketQueue> queue);
    ~AudioCaptureWorker() override;
    void stop();

    struct Status
    {
        QString state = QStringLiteral("starting");
        QString error;
    };

    Status status() const;

    QString deviceId() const
    {
        return m_deviceId;
    }

protected:
    void run() override;

private:
    void setStatus(const QString &state, const QString &error = {});
    QString m_deviceId;
    AudioSourceType m_type;
    std::shared_ptr<AudioPacketQueue> m_queue;
    mutable std::mutex m_mutex;
    Status m_status;
};
#endif
