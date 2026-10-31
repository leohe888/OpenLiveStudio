#include "video/source.h"

#include <type_traits>

bool CameraConfig::valid() const
{
    return !deviceId.trimmed().isEmpty() && frameRateDenominator > 0
        && ((requestedResolution.isEmpty() && requestedFrameRate == 0)
            || (requestedResolution.isValid() && !requestedResolution.isEmpty() && requestedFrameRate > 0));
}

bool CameraConfig::operator==(const CameraConfig &other) const
{
    return deviceId == other.deviceId && requestedResolution == other.requestedResolution
        && requestedFrameRate == other.requestedFrameRate && frameRateDenominator == other.frameRateDenominator;
}

bool CameraConfig::operator!=(const CameraConfig &other) const
{
    return !(*this == other);
}

SourceType SourceData::type() const
{
    return std::visit([](const auto &source) {
        using Source = std::decay_t<decltype(source)>;
        if constexpr (std::is_same_v<Source, DisplaySourceData>)
            return SourceType::DisplayCapture;
        else if constexpr (std::is_same_v<Source, CameraSourceData>)
            return SourceType::Camera;
        else {
            static_assert(std::is_same_v<Source, ImageSourceData>);
            return SourceType::Image;
        }
    }, details);
}

QSize SourceData::originalSize() const
{
    return std::visit([](const auto &source) { return source.runtime.originalSize; }, details);
}
