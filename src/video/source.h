#ifndef SOURCE_H
#define SOURCE_H

#include "video/sourcetransform.h"
#include "video/sourcefilter.h"

#include <QSize>
#include <QString>
#include <variant>

enum class SourceType {
    DisplayCapture,
    Camera,
    Image
};

struct DisplayConfig {
    QString displayId;
    bool captureCursor = true;
};
struct CameraConfig {
    QString deviceId;
    // Invalid size and zero frame rate mean no explicit preference.
    QSize requestedResolution;
    int requestedFrameRate = 0;
    int frameRateDenominator = 1;
    bool valid() const;
    bool operator==(const CameraConfig &other) const;
    bool operator!=(const CameraConfig &other) const;
};
struct ImageConfig {
    QString filePath;
};
struct DisplayRuntimeInfo {
    QSize originalSize;
    QString captureState;
    QString captureError;
};
struct ImageRuntimeInfo {
    QSize originalSize;
    QString loadState;
    QString loadError;
};
struct CameraRuntimeInfo {
    QSize originalSize;
    QString captureState;
    QString captureError;
    int actualRateNumerator = 0, actualRateDenominator = 1;
};
struct ImageSourceData {
    ImageConfig config;
    ImageRuntimeInfo runtime;
};
struct DisplaySourceData {
    DisplayConfig config;
    DisplayRuntimeInfo runtime;
};
struct CameraSourceData {
    CameraConfig config;
    CameraRuntimeInfo runtime;
};
using SourceDetails = std::variant<DisplaySourceData, CameraSourceData, ImageSourceData>;

struct SourceData {
    QString id;
    QString name;
    SourceDetails details = DisplaySourceData{};
    bool visible = true;
    bool locked = false;
    SourceTransform transform;
    QList<SourceFilter> filters;

    SourceType type() const;
    QSize originalSize() const;
};

#endif // SOURCE_H
