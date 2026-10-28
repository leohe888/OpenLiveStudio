#ifndef AUDIOCAPTUREWORKER_H
#define AUDIOCAPTUREWORKER_H
#include "audiobuffer.h"
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <condition_variable>

QVariantList audioDevices(QString *error);

class AudioCaptureWorker : public QThread {
public:
    AudioCaptureWorker(QString deviceId, QString type, std::shared_ptr<AudioPacketQueue> queue);
    ~AudioCaptureWorker() override;
    void stop();
    struct Status { QString state = QStringLiteral("starting"); QString error; };
    Status status() const;
    QString deviceId() const { return m_deviceId; }
protected:
    void run() override;
private:
    QString capture();
    void setStatus(const QString &state, const QString &error = {});
    QString m_deviceId, m_type;
    std::shared_ptr<AudioPacketQueue> m_queue;
    std::atomic_bool m_stop{false};
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    Status m_status;
};
#endif
