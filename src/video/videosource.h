#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include "video/videosourcetransform.h"
#include "video/videosourcefilter.h"

#include <QSize>
#include <QString>

enum class VideoSourceType {
    DisplayCapture,
    Camera,
    Image
};

enum class VideoSourceState {
    Idle,
    Loading,
    Ready,
    Starting,
    Capturing,
    Paused,
    Error
};

// 来源是模型对象，采集设备、线程与帧仍由控制器持有。
class VideoSource
{
public:
    virtual ~VideoSource() = default;
    virtual VideoSourceType type() const = 0;

    const QString &id() const;
    const QString &name() const;
    void setName(const QString &name);
    bool visible() const;
    void setVisible(bool visible);
    bool locked() const;
    void setLocked(bool locked);
    const VideoSourceTransform &transform() const;
    void setTransform(const VideoSourceTransform &transform);
    const QList<VideoSourceFilter> &filters() const;
    void setFilters(const QList<VideoSourceFilter> &filters);

    QSize pixelSize() const;
    VideoSourceState state() const;
    const QString &error() const;
    // 状态及错误作为同一次更新发布，不暴露独立的状态/错误 setter。
    void updateStatus(const QSize &pixelSize, VideoSourceState state, const QString &error = {});

protected:
    VideoSource(const QString &id, const QString &name);

private:
    QString m_id;
    QString m_name;
    bool m_visible = true;
    bool m_locked = false;
    VideoSourceTransform m_transform;
    QList<VideoSourceFilter> m_filters;
    QSize m_pixelSize;
    VideoSourceState m_state = VideoSourceState::Idle;
    QString m_error;
};

class DisplaySource final : public VideoSource
{
public:
    DisplaySource(const QString &id, const QString &name, bool captureCursor = true);
    VideoSourceType type() const override;
    bool captureCursor() const;
    void setCaptureCursor(bool captureCursor);

private:
    bool m_captureCursor = true;
};

class CameraSource final : public VideoSource
{
public:
    CameraSource(const QString &id, const QString &name, const QString &deviceId,
                 const QSize &resolution = {}, int frameRateNumerator = 0,
                 int frameRateDenominator = 1, const QString &pixelFormat = {});
    VideoSourceType type() const override;

    const QString &deviceId() const;
    void setDeviceId(const QString &deviceId);
    const QString &pixelFormat() const;
    void setPixelFormat(const QString &format);
    // 空分辨率与零帧率表示自动选择设备模式。
    const QSize &resolution() const;
    void setResolution(const QSize &resolution);
    int frameRateNumerator() const;
    void setFrameRateNumerator(int numerator);
    int frameRateDenominator() const;
    void setFrameRateDenominator(int denominator);
    bool hasValidCaptureSettings() const;
    bool hasSameCaptureSettings(const CameraSource &other) const;
    void copyCaptureSettings(const CameraSource &other);

    int negotiatedFrameRateNumerator() const;
    int negotiatedFrameRateDenominator() const;
    const QString &captureDescription() const;
    void updateCapture(const QSize &pixelSize, int numerator, int denominator,
                       const QString &description = {});

private:
    QString m_deviceId;
    QString m_pixelFormat;
    QSize m_resolution;
    int m_frameRateNumerator = 0;
    int m_frameRateDenominator = 1;
    int m_negotiatedFrameRateNumerator = 0;
    int m_negotiatedFrameRateDenominator = 1;
    QString m_captureDescription;
};

class ImageSource final : public VideoSource
{
public:
    ImageSource(const QString &id, const QString &name, const QString &filePath);
    VideoSourceType type() const override;
    const QString &filePath() const;
    void setFilePath(const QString &filePath);

private:
    QString m_filePath;
};

#endif
