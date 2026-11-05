#ifndef CAMERACAPTUREWORKER_H
#define CAMERACAPTUREWORKER_H

#include "video/videosource.h"
#include "graphics/captureframe.h"
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
    int frameRateNumerator = 0;
    int frameRateDenominator = 1;
    QString subtype;
    QString name;

    bool isValid() const
    {
        return width > 0 && height > 0 && frameRateNumerator > 0 && frameRateDenominator > 0;
    }

    bool supported = true;

    bool sameFormat(const CameraMode &other) const
    {
        return subtype == other.subtype && width == other.width && height == other.height &&
               qint64(frameRateNumerator) * other.frameRateDenominator ==
                   qint64(other.frameRateNumerator) * frameRateDenominator;
    }

    CameraSourceConfig config(const QString &deviceId) const
    {
        auto result = width > 0 ? CameraSourceConfig{deviceId, QSize(width, height),
                                                     frameRateNumerator, frameRateDenominator}
                                : CameraSourceConfig{deviceId};
        result.setPixelFormat(subtype);
        return result;
    }
};
Q_DECLARE_METATYPE(CameraMode)

struct CameraFrame
{
    std::shared_ptr<const CaptureFrame> renderFrame;
    QString status;
    qint64 timestampNs = 0;
    quint64 sequence = 0, generation = 0;
    int frameRateNumerator = 0, frameRateDenominator = 1;
};

struct AVFrame;
std::shared_ptr<const PixelFrame> cameraPixels(const AVFrame *frame, bool &inferred);

// V4L2 设备和原生模式查询在后台线程执行。
QList<CameraDevice> cameraDevices(QString *error = nullptr);
QList<CameraMode> cameraModes(const QString &deviceId, QString *error = nullptr);

// 在界面线程管理生命周期；V4L2 设备和解码器仅由 run() 线程持有。
// stop() 只请求停止；析构等待退出。takeLatest()/lastError() 受锁保护，
// 返回的帧由共享所有权保持存活，读取后不应修改。
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
        return m_config.deviceId();
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
