#include "video/videosource.h"

#include <type_traits>

bool CameraSourceConfig::isValid() const
{
    return !deviceId.trimmed().isEmpty() && frameRateDenominator > 0 &&
           ((resolution.isEmpty() && frameRateNumerator == 0) ||
            (resolution.isValid() && !resolution.isEmpty() && frameRateNumerator > 0));
}

bool CameraSourceConfig::operator==(const CameraSourceConfig &other) const
{
    return deviceId == other.deviceId && resolution == other.resolution &&
           frameRateNumerator == other.frameRateNumerator &&
           frameRateDenominator == other.frameRateDenominator;
}

bool CameraSourceConfig::operator!=(const CameraSourceConfig &other) const
{
    return !(*this == other);
}

VideoSourceType VideoSource::type() const
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
