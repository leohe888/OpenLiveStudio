#ifndef CAMERACAPTUREWORKER_H
#define CAMERACAPTUREWORKER_H

#include "video/videosource.h"
#include <QImage>
#include <QMutex>
#include <QThread>
#include <QMetaType>
#include <atomic>
#include <memory>

struct CameraDevice
{
    QString id;
    QString name;
};

struct CameraMode
{
    int width = 0;
    int height = 0;
    int numerator = 0;
    int denominator = 1;
    QString subtype;
    QString name;

    bool isValid() const
    {
        return width > 0 && height > 0 && numerator > 0 && denominator > 0;
    }

    bool sameFormat(const CameraMode &other) const
    {
        return width == other.width && height == other.height &&
               qint64(numerator) * other.denominator == qint64(other.numerator) * denominator;
    }

    CameraSourceConfig config(const QString &deviceId) const
    {
        return width > 0 ? CameraSourceConfig{deviceId, QSize(width, height), numerator, denominator}
                         : CameraSourceConfig{deviceId};
    }
};
Q_DECLARE_METATYPE(CameraMode)

struct CameraFrame
{
    QImage image;
    qint64 timestampNs = 0;
    qint64 deviceTimestamp100ns = 0;
    quint64 sequence = 0, generation = 0;
    int rateNumerator = 0, rateDenominator = 1;
};

// Each function initializes and shuts down COM/MF on its calling background thread.
QList<CameraDevice> cameraDevices(QString *error = nullptr);
QList<CameraMode> cameraModes(const QString &deviceId, QString *error = nullptr);

class CameraCaptureWorker : public QThread
{
public:
    CameraCaptureWorker(CameraSourceConfig config, quint64 generation);
    ~CameraCaptureWorker() override;
    void stop();
    std::shared_ptr<const CameraFrame> takeLatest();
    QString lastError() const;

    const QString &deviceId() const
    {
        return m_config.deviceId;
    }

protected:
    void run() override;

private:
    void fail(const QString &error);
    CameraSourceConfig m_config;
    quint64 m_generation;
    std::atomic_bool m_stop{false};
    mutable QMutex m_mutex;
    std::shared_ptr<const CameraFrame> m_latest;
    QString m_error;
};
#endif
