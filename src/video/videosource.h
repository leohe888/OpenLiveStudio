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

struct DisplayConfig
{
    QString displayId;
    bool captureCursor = true;
};

struct CameraConfig
{
    QString deviceId;
    // Invalid size and zero frame rate mean no explicit preference.
    QSize requestedResolution;
    int requestedFrameRateNumerator = 0;
    int requestedFrameRateDenominator = 1;
    bool isValid() const;
    bool operator==(const CameraConfig &other) const;
    bool operator!=(const CameraConfig &other) const;
};

struct ImageConfig
{
    QString filePath;
};

struct DisplayRuntime
{
    QSize pixelSize;
    QString captureState;
    QString captureError;
};

struct ImageRuntime
{
    QSize pixelSize;
    QString loadState;
    QString loadError;
};

struct CameraRuntime
{
    QSize pixelSize;
    QString captureState;
    QString captureError;
    int actualFrameRateNumerator = 0;
    int actualFrameRateDenominator = 1;
};

struct ImageSourceDetails
{
    ImageConfig config;
    ImageRuntime runtime;
};

struct DisplaySourceDetails
{
    DisplayConfig config;
    DisplayRuntime runtime;
};

struct CameraSourceDetails
{
    CameraConfig config;
    CameraRuntime runtime;
};

using VideoSourceDetails =
    std::variant<DisplaySourceDetails, CameraSourceDetails, ImageSourceDetails>;

struct VideoSource
{
    QString id;
    QString name;
    VideoSourceDetails details = DisplaySourceDetails{};
    bool visible = true;
    bool locked = false;
    VideoSourceTransform transform;
    QList<VideoSourceFilter> filters;

    VideoSourceType type() const;
    QSize pixelSize() const;
};

#endif // VIDEOSOURCE_H
