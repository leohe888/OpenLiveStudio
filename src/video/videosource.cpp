#include "video/videosource.h"

#include <type_traits>

bool CameraSourceParams::isValid() const
{
    return !deviceId.trimmed().isEmpty() && requestedFrameRateDenominator > 0 &&
           ((requestedResolution.isEmpty() && requestedFrameRateNumerator == 0) ||
            (requestedResolution.isValid() && !requestedResolution.isEmpty() &&
             requestedFrameRateNumerator > 0));
}

bool CameraSourceParams::operator==(const CameraSourceParams &other) const
{
    return deviceId == other.deviceId && requestedResolution == other.requestedResolution &&
           requestedFrameRateNumerator == other.requestedFrameRateNumerator &&
           requestedFrameRateDenominator == other.requestedFrameRateDenominator;
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
        params);
}
