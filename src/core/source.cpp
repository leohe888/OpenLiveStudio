#include "source.h"

#include <type_traits>

SourceType SourceData::type() const
{
    return std::visit([](const auto &config) {
        using Config = std::decay_t<decltype(config)>;
        if constexpr (std::is_same_v<Config, DisplayCaptureConfig>)
            return SourceType::DisplayCapture;
        else if constexpr (std::is_same_v<Config, WindowCaptureConfig>)
            return SourceType::WindowCapture;
        else if constexpr (std::is_same_v<Config, CameraConfig>)
            return SourceType::Camera;
        else if constexpr (std::is_same_v<Config, ImageConfig>)
            return SourceType::Image;
        else {
            static_assert(std::is_same_v<Config, TextConfig>);
            return SourceType::Text;
        }
    }, config);
}
