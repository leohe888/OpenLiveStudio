#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include "video/videosourcetransform.h"
#include "video/videosourcefilter.h"

#include <QSize>
#include <QString>
#include <variant>

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

class DisplaySourceConfig
{
public:
    DisplaySourceConfig();
    DisplaySourceConfig(const QString &displayId);
    DisplaySourceConfig(const QString &displayId, bool captureCursor);

    const QString &displayId() const;
    void setDisplayId(const QString &displayId);
    bool captureCursor() const;
    void setCaptureCursor(bool captureCursor);

private:
    QString m_displayId;
    bool m_captureCursor = true;
};

class CameraSourceConfig
{
public:
    CameraSourceConfig();
    CameraSourceConfig(const QString &deviceId);
    CameraSourceConfig(const QString &deviceId, const QSize &resolution, int frameRateNumerator,
                       int frameRateDenominator);

    const QString &deviceId() const;
    void setDeviceId(const QString &deviceId);
    // 空分辨率和零帧率表示采用设备默认模式，不代表配置无效。
    const QSize &resolution() const;
    void setResolution(const QSize &resolution);
    int frameRateNumerator() const;
    void setFrameRateNumerator(int frameRateNumerator);
    int frameRateDenominator() const;
    void setFrameRateDenominator(int frameRateDenominator);

    bool isValid() const;
    bool operator==(const CameraSourceConfig &other) const;
    bool operator!=(const CameraSourceConfig &other) const;

private:
    QString m_deviceId;
    QSize m_resolution;
    int m_frameRateNumerator = 0;
    int m_frameRateDenominator = 1;
};

class ImageSourceConfig
{
public:
    ImageSourceConfig();
    ImageSourceConfig(const QString &filePath);

    const QString &filePath() const;
    void setFilePath(const QString &filePath);

private:
    QString m_filePath;
};

// 来源类型由配置的 variant 分支决定，避免另存一份类型字段后出现不一致。
using VideoSourceConfig = std::variant<DisplaySourceConfig, CameraSourceConfig, ImageSourceConfig>;

class VideoSourceRuntime
{
public:
    VideoSourceRuntime();
    VideoSourceRuntime(const QSize &pixelSize, VideoSourceState state, const QString &error);
    VideoSourceRuntime(const QSize &pixelSize, VideoSourceState state, const QString &error,
                       int actualFrameRateNumerator, int actualFrameRateDenominator);

    const QSize &pixelSize() const;
    void setPixelSize(const QSize &pixelSize);
    VideoSourceState state() const;
    void setState(VideoSourceState state);
    const QString &error() const;
    void setError(const QString &error);
    // 仅摄像头来源提供实际帧率。
    int actualFrameRateNumerator() const;
    void setActualFrameRateNumerator(int actualFrameRateNumerator);
    int actualFrameRateDenominator() const;
    void setActualFrameRateDenominator(int actualFrameRateDenominator);

private:
    QSize m_pixelSize;
    VideoSourceState m_state = VideoSourceState::Idle;
    QString m_error;
    int m_actualFrameRateNumerator = 0;
    int m_actualFrameRateDenominator = 1;
};

class VideoSource
{
public:
    VideoSource();
    VideoSource(const QString &id, const QString &name);
    VideoSource(const QString &id, const QString &name, const VideoSourceConfig &config);
    VideoSource(const QString &id, const QString &name, bool visible, bool locked,
                const VideoSourceTransform &transform,
                const QList<VideoSourceFilter> &filters, const VideoSourceRuntime &runtime,
                const VideoSourceConfig &config);

    const QString &id() const;
    void setId(const QString &id);
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
    const VideoSourceRuntime &runtime() const;
    void setRuntime(const VideoSourceRuntime &runtime);
    const VideoSourceConfig &config() const;
    void setConfig(const VideoSourceConfig &config);

    VideoSourceType type() const;

private:
    QString m_id;
    QString m_name;
    bool m_visible = true;
    bool m_locked = false;
    VideoSourceTransform m_transform;
    QList<VideoSourceFilter> m_filters;
    VideoSourceRuntime m_runtime;
    VideoSourceConfig m_config = DisplaySourceConfig{};
};

#endif
