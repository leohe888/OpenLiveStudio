#ifndef VIDEOSOURCEDEFINITION_H
#define VIDEOSOURCEDEFINITION_H

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

struct DisplaySourceConfig
{
    QString displayId;
    bool captureCursor = true;
};

struct CameraSourceConfig
{
    QString deviceId;
    // Invalid size and zero frame rate mean no explicit preference.
    QSize requestedResolution;
    int requestedFrameRateNumerator = 0;
    int requestedFrameRateDenominator = 1;
    bool isValid() const;
    bool operator==(const CameraSourceConfig &other) const;
    bool operator!=(const CameraSourceConfig &other) const;
};

struct ImageSourceConfig
{
    QString filePath;
};

using VideoSourceConfig =
    std::variant<DisplaySourceConfig, CameraSourceConfig, ImageSourceConfig>;

struct VideoSourceDefinition
{
    QString id;
    QString name;
    VideoSourceConfig config = DisplaySourceConfig{};
    bool visible = true;
    bool locked = false;
    VideoSourceTransform transform;
    QList<VideoSourceFilter> filters;

    VideoSourceType type() const;
};

#endif // VIDEOSOURCEDEFINITION_H
