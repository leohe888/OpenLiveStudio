#include "video/videosourcefilter.h"

#include <cmath>
#include <type_traits>

bool ColorCorrectionConfig::isIdentity() const
{
    return brightness == 0 && contrast == 1 && saturation == 1;
}

bool ColorCorrectionConfig::isValid() const
{
    return std::isfinite(brightness) && brightness >= -1 && brightness <= 1 &&
           std::isfinite(contrast) && contrast >= 0 && contrast <= 2 &&
           std::isfinite(saturation) && saturation >= 0 && saturation <= 2;
}

bool ColorCorrectionConfig::operator==(const ColorCorrectionConfig &other) const
{
    return brightness == other.brightness && contrast == other.contrast &&
           saturation == other.saturation;
}

VideoSourceFilterType VideoSourceFilter::type() const
{
    return std::visit([](const auto &value) {
        using Config = std::decay_t<decltype(value)>;
        static_assert(std::is_same_v<Config, ColorCorrectionConfig>);
        return VideoSourceFilterType::ColorCorrection;
    }, config);
}
