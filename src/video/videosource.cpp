#include "video/videosource.h"

#include <type_traits>

bool CameraConfig::isValid() const
{
    return !deviceId.trimmed().isEmpty() && requestedFrameRateDenominator > 0 &&
           ((requestedResolution.isEmpty() && requestedFrameRateNumerator == 0) ||
            (requestedResolution.isValid() && !requestedResolution.isEmpty() &&
             requestedFrameRateNumerator > 0));
}

bool CameraConfig::operator==(const CameraConfig &other) const
{
    return deviceId == other.deviceId && requestedResolution == other.requestedResolution &&
           requestedFrameRateNumerator == other.requestedFrameRateNumerator &&
           requestedFrameRateDenominator == other.requestedFrameRateDenominator;
}

bool CameraConfig::operator!=(const CameraConfig &other) const
{
    return !(*this == other);
}

VideoSourceType VideoSource::type() const
{
    return std::visit(
        [](const auto &source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, DisplaySourceDetails>)
                return VideoSourceType::DisplayCapture;
            else if constexpr (std::is_same_v<Source, CameraSourceDetails>)
                return VideoSourceType::Camera;
            else {
                static_assert(std::is_same_v<Source, ImageSourceDetails>);
                return VideoSourceType::Image;
            }
        },
        details);
}

QSize VideoSource::pixelSize() const
{
    return std::visit(
        [](const auto &source) {
            return source.runtime.pixelSize;
        },
        details);
}
