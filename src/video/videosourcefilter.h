#ifndef VIDEOSOURCEFILTER_H
#define VIDEOSOURCEFILTER_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <variant>

enum class VideoSourceFilterType {
    ColorCorrection
};

struct ColorCorrectionConfig
{
    float brightness = 0;
    float contrast = 1;
    float saturation = 1;

    bool isIdentity() const;
    bool isValid() const;
    bool operator==(const ColorCorrectionConfig &other) const;
};

using VideoSourceFilterConfig = std::variant<ColorCorrectionConfig>;

struct VideoSourceFilter
{
    QString id;
    bool enabled = true;
    VideoSourceFilterConfig config = ColorCorrectionConfig{};

    VideoSourceFilterType type() const;
};
Q_DECLARE_METATYPE(VideoSourceFilter)
Q_DECLARE_METATYPE(QList<VideoSourceFilter>)

#endif
