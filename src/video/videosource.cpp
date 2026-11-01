#include "video/videosource.h"

#include <type_traits>

bool CameraSourceParams::isValid() const
{
    return !deviceId.trimmed().isEmpty() && frameRateDenominator > 0 &&
           ((resolution.isEmpty() && frameRateNumerator == 0) ||
            (resolution.isValid() && !resolution.isEmpty() && frameRateNumerator > 0));
}

bool CameraSourceParams::operator==(const CameraSourceParams &other) const
{
    return deviceId == other.deviceId && resolution == other.resolution &&
           frameRateNumerator == other.frameRateNumerator &&
           frameRateDenominator == other.frameRateDenominator;
}

bool CameraSourceParams::operator!=(const CameraSourceParams &other) const
{
    return !(*this == other);
}

VideoSourceType VideoSource::type() const
{
    return std::visit(
        [](const auto &source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, DisplaySourceParams>)
                return VideoSourceType::DisplayCapture;
            else if constexpr (std::is_same_v<Source, CameraSourceParams>)
                return VideoSourceType::Camera;
            else {
                static_assert(std::is_same_v<Source, ImageSourceParams>);
                return VideoSourceType::Image;
            }
        },
        config);
}
