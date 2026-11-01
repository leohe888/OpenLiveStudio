#include "video/videosourcedefinition.h"

#include <type_traits>

bool CameraSourceConfig::isValid() const
{
    return !deviceId.trimmed().isEmpty() && requestedFrameRateDenominator > 0 &&
           ((requestedResolution.isEmpty() && requestedFrameRateNumerator == 0) ||
            (requestedResolution.isValid() && !requestedResolution.isEmpty() &&
             requestedFrameRateNumerator > 0));
}

bool CameraSourceConfig::operator==(const CameraSourceConfig &other) const
{
    return deviceId == other.deviceId && requestedResolution == other.requestedResolution &&
           requestedFrameRateNumerator == other.requestedFrameRateNumerator &&
           requestedFrameRateDenominator == other.requestedFrameRateDenominator;
}

bool CameraSourceConfig::operator!=(const CameraSourceConfig &other) const
{
    return !(*this == other);
}

VideoSourceType VideoSourceDefinition::type() const
{
    return std::visit(
        [](const auto &source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, DisplaySourceConfig>)
                return VideoSourceType::DisplayCapture;
            else if constexpr (std::is_same_v<Source, CameraSourceConfig>)
                return VideoSourceType::Camera;
            else {
                static_assert(std::is_same_v<Source, ImageSourceConfig>);
                return VideoSourceType::Image;
            }
        },
        config);
}
