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
    bool valid() const {
        return !deviceId.trimmed().isEmpty() && frameRateDenominator > 0
            && ((requestedResolution.isEmpty() && requestedFrameRate == 0)
                || (requestedResolution.isValid() && !requestedResolution.isEmpty() && requestedFrameRate > 0));
    }
    bool operator==(const CameraConfig &other) const {
        return deviceId == other.deviceId && requestedResolution == other.requestedResolution
            && requestedFrameRate == other.requestedFrameRate && frameRateDenominator == other.frameRateDenominator;
    }
    bool operator!=(const CameraConfig &other) const { return !(*this == other); }
};
struct ImageConfig {
    QString filePath;
};
using SourceConfig = std::variant<
    DisplayConfig,
    CameraConfig,
    ImageConfig
>;

struct SourceRuntimeInfo {
    // Invalid until loading, capture, or rendering supplies the actual size.
    QSize originalSize;
    QString captureState;
    QString captureError;
    int actualRateNumerator = 0, actualRateDenominator = 1;
};
struct SourceData {
    QString id;
    QString name;
    SourceConfig config = DisplayConfig{};
    SourceRuntimeInfo runtime;
    bool visible = true;
    bool locked = false;
    SourceTransform transform;
    QList<SourceFilter> filters;

    SourceType type() const;
};

#endif // SOURCE_H
