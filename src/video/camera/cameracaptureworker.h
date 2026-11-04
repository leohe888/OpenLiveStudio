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
    qint64 deviceTimestamp100ns = 0;
    quint64 sequence = 0, generation = 0;
    int frameRateNumerator = 0, frameRateDenominator = 1;
};

struct AVFrame;
std::shared_ptr<const PixelFrame> cameraPixels(const AVFrame *frame, bool &inferred);

#ifdef Q_OS_WIN
struct IMFSample;
struct IMFMediaType;
struct AVCodecParameters;
struct AVPacket;
std::shared_ptr<const PixelFrame> cameraMfPixels(IMFSample *sample, IMFMediaType *type,
                                                 bool &inferred);
// 输出参数对象必须为空；保留 H.264 初始化数据及 MF 色彩元数据。
void cameraMfParameters(IMFMediaType *type, AVCodecParameters *parameters);
void cameraMfPacket(IMFSample *sample, AVPacket *packet, qint64 timestamp100ns, bool intraFrame);
#endif

// Media Foundation 的设备和原生模式查询在后台线程执行。
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

    void useSoftwareDecoder()
    {
        m_forceSoftware.store(true);
    }

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
    std::atomic_bool m_stop{false}, m_forceSoftware{false};
    mutable QMutex m_mutex;
    std::shared_ptr<const CameraFrame> m_latest;
    QString m_error;
};
#endif
