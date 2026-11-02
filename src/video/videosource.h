#ifndef VIDEOSOURCE_H
#define VIDEOSOURCE_H

#include "video/videosourcecanvasgeometry.h"
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
    QSize resolution;
    int frameRateNumerator = 0;
    int frameRateDenominator = 1;
    bool isValid() const;
    bool operator==(const CameraSourceConfig &other) const;
    bool operator!=(const CameraSourceConfig &other) const;
};

struct ImageSourceConfig
{
    QString filePath;
};

// Configuration owned by the source kind; the active alternative decides VideoSource::type().
using VideoSourceConfig = std::variant<DisplaySourceConfig, CameraSourceConfig, ImageSourceConfig>;

struct VideoSource
{
    QString id;
    QString name;
    VideoSourceConfig config = DisplaySourceConfig{};
    bool visible = true;
    bool locked = false;
    VideoSourceCanvasGeometry canvasGeometry;
    QList<VideoSourceFilter> filters;

    VideoSourceType type() const;
};

#endif // VIDEOSOURCE_H
