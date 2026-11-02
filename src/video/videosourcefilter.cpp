#include "video/videosourcefilter.h"

#include <cmath>
#include <type_traits>

ColorCorrectionConfig::ColorCorrectionConfig() = default;

ColorCorrectionConfig::ColorCorrectionConfig(float brightness, float contrast, float saturation)
    : m_brightness(brightness), m_contrast(contrast), m_saturation(saturation)
{
}

VideoSourceFilter::VideoSourceFilter() = default;

VideoSourceFilter::VideoSourceFilter(const QString &id) : m_id(id)
{
}

VideoSourceFilter::VideoSourceFilter(const QString &id, bool enabled,
                                     const VideoSourceFilterConfig &config)
    : m_id(id), m_enabled(enabled), m_config(config)
{
}

float ColorCorrectionConfig::brightness() const
{
    return m_brightness;
}

void ColorCorrectionConfig::setBrightness(float brightness)
{
    m_brightness = brightness;
}

float ColorCorrectionConfig::contrast() const
{
    return m_contrast;
}

void ColorCorrectionConfig::setContrast(float contrast)
{
    m_contrast = contrast;
}

float ColorCorrectionConfig::saturation() const
{
    return m_saturation;
}

void ColorCorrectionConfig::setSaturation(float saturation)
{
    m_saturation = saturation;
}

const QString &VideoSourceFilter::id() const
{
    return m_id;
}

void VideoSourceFilter::setId(const QString &id)
{
    m_id = id;
}

bool VideoSourceFilter::enabled() const
{
    return m_enabled;
}

void VideoSourceFilter::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

const VideoSourceFilterConfig &VideoSourceFilter::config() const
{
    return m_config;
}

void VideoSourceFilter::setConfig(const VideoSourceFilterConfig &config)
{
    m_config = config;
}

bool ColorCorrectionConfig::isIdentity() const
{
    return m_brightness == 0 && m_contrast == 1 && m_saturation == 1;
}

bool ColorCorrectionConfig::isValid() const
{
    return std::isfinite(m_brightness) && m_brightness >= -1 && m_brightness <= 1 &&
           std::isfinite(m_contrast) && m_contrast >= 0 && m_contrast <= 2 &&
           std::isfinite(m_saturation) && m_saturation >= 0 && m_saturation <= 2;
}

bool ColorCorrectionConfig::operator==(const ColorCorrectionConfig &other) const
{
    return m_brightness == other.m_brightness && m_contrast == other.m_contrast &&
           m_saturation == other.m_saturation;
}

bool ColorCorrectionConfig::operator!=(const ColorCorrectionConfig &other) const
{
    return !(*this == other);
}

VideoSourceFilterType VideoSourceFilter::type() const
{
    return std::visit(
        [](const auto &value) {
            using Config = std::decay_t<decltype(value)>;
            static_assert(std::is_same_v<Config, ColorCorrectionConfig>);
            return VideoSourceFilterType::ColorCorrection;
        },
        m_config);
}
