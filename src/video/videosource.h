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

struct DisplaySourceParams
{
    QString displayId;
    bool captureCursor = true;
};

struct CameraSourceParams
{
    QString deviceId;
    // Invalid size and zero frame rate mean no explicit preference.
    QSize requestedResolution;
    int requestedFrameRateNumerator = 0;
    int requestedFrameRateDenominator = 1;
    bool isValid() const;
    bool operator==(const CameraSourceParams &other) const;
    bool operator!=(const CameraSourceParams &other) const;
};

struct ImageSourceParams
{
    QString filePath;
};

using VideoSourceParams =
    std::variant<DisplaySourceParams, CameraSourceParams, ImageSourceParams>;

struct VideoSource
{
    QString id;
    QString name;
    VideoSourceParams params = DisplaySourceParams{};
    bool visible = true;
    bool locked = false;
    VideoSourceTransform transform;
    QList<VideoSourceFilter> filters;

    VideoSourceType type() const;
};

#endif // VIDEOSOURCE_H
