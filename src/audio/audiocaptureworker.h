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

// 在所属（界面）线程创建和销毁；PulseAudio 会话及采集回调只在 run() 线程执行。
// stop() 仅请求退出，析构等待线程结束；queue 由共享所有权保持存活。
// status() 返回受锁保护的副本，可跨线程读取。
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
