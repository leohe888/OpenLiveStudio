#ifndef VIDEOSOURCEFILTER_H
#define VIDEOSOURCEFILTER_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <variant>

enum class VideoSourceFilterType {
    ColorCorrection
};

class ColorCorrectionConfig
{
public:
    ColorCorrectionConfig();
    ColorCorrectionConfig(float brightness, float contrast, float saturation);

    float brightness() const;
    void setBrightness(float brightness);
    float contrast() const;
    void setContrast(float contrast);
    float saturation() const;
    void setSaturation(float saturation);
    bool isIdentity() const;
    bool isValid() const;
    bool operator==(const ColorCorrectionConfig &other) const;
    bool operator!=(const ColorCorrectionConfig &other) const;

private:
    float m_brightness = 0;
    float m_contrast = 1;
    float m_saturation = 1;
};

using VideoSourceFilterConfig = std::variant<ColorCorrectionConfig>;

class VideoSourceFilter
{
public:
    VideoSourceFilter();
    VideoSourceFilter(const QString &id);
    VideoSourceFilter(const QString &id, bool enabled, const VideoSourceFilterConfig &config);

    const QString &id() const;
    void setId(const QString &id);
    bool enabled() const;
    void setEnabled(bool enabled);
    const VideoSourceFilterConfig &config() const;
    void setConfig(const VideoSourceFilterConfig &config);
    VideoSourceFilterType type() const;

private:
    QString m_id;
    bool m_enabled = true;
    VideoSourceFilterConfig m_config = ColorCorrectionConfig{};
};
Q_DECLARE_METATYPE(VideoSourceFilter)
Q_DECLARE_METATYPE(QList<VideoSourceFilter>)

#endif
