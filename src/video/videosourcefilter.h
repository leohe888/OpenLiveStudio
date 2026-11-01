#ifndef VIDEOSOURCEFILTER_H
#define VIDEOSOURCEFILTER_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <cmath>

enum class VideoSourceFilterType {
    ColorCorrection
};

struct VideoSourceFilter
{
    QString id;
    VideoSourceFilterType type = VideoSourceFilterType::ColorCorrection;
    bool enabled = true;
    float brightness = 0;
    float contrast = 1;
    float saturation = 1;

    bool isIdentity() const
    {
        return brightness == 0 && contrast == 1 && saturation == 1;
    }

    bool isValid() const
    {
        return type == VideoSourceFilterType::ColorCorrection && std::isfinite(brightness) &&
               brightness >= -1 && brightness <= 1 && std::isfinite(contrast) && contrast >= 0 &&
               contrast <= 2 && std::isfinite(saturation) && saturation >= 0 && saturation <= 2;
    }
};
Q_DECLARE_METATYPE(VideoSourceFilter)
Q_DECLARE_METATYPE(QList<VideoSourceFilter>)

#endif
