#ifndef CAMERACAPTUREWORKER_H
#define CAMERACAPTUREWORKER_H

#include "video/source.h"
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QVariantList>
#include <atomic>
#include <memory>

struct CameraFrame {
    QImage image;
    qint64 timestampNs = 0;
    qint64 deviceTimestamp100ns = 0;
    quint64 sequence = 0, generation = 0;
    int rateNumerator = 0, rateDenominator = 1;
};

// Each function initializes and shuts down COM/MF on its calling background thread.
QVariantList cameraDevices(QString *error = nullptr);
QVariantList cameraModes(const QString &deviceId, QString *error = nullptr);

class CameraCaptureWorker : public QThread {
public:
    CameraCaptureWorker(CameraConfig config, quint64 generation);
    ~CameraCaptureWorker() override;
    void stop();
    std::shared_ptr<const CameraFrame> takeLatest();
    QString error() const;
    const QString &deviceId() const { return m_config.deviceId; }
protected:
    void run() override;
private:
    void fail(const QString &error);
    CameraConfig m_config;
    quint64 m_generation;
    std::atomic_bool m_stop{false};
    mutable QMutex m_mutex;
    std::shared_ptr<const CameraFrame> m_latest;
    QString m_error;
};
#endif
