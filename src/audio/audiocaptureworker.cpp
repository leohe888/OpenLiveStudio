#include "audio/audiocaptureworker.h"

QList<AudioDevice> audioDevices(QString *error)
{
    QList<AudioDevice> result;
    *error = QStringLiteral("Linux 音频采集后端尚未接入。");
    return result;
}

AudioCaptureWorker::AudioCaptureWorker(QString deviceId, AudioSourceType type,
                                       std::shared_ptr<AudioPacketQueue> queue)
    : m_deviceId(std::move(deviceId)), m_queue(std::move(queue))
{
    Q_UNUSED(type);
    setObjectName(QStringLiteral("AudioCapture"));
}

AudioCaptureWorker::~AudioCaptureWorker()
{
    stop();
    wait();
}

void AudioCaptureWorker::stop()
{
    requestInterruption();
}

AudioCaptureWorker::Status AudioCaptureWorker::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void AudioCaptureWorker::setStatus(const QString &state, const QString &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = {state, error};
}

void AudioCaptureWorker::run()
{
    setPriority(QThread::HighPriority);
    setStatus("error", QStringLiteral("Linux 音频采集后端尚未接入。"));
    m_queue->clear();
}
