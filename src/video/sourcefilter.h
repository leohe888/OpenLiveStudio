#ifndef SOURCEFILTER_H
#define SOURCEFILTER_H

#include <QList>
#include <QMetaType>
#include <QString>
#include <cmath>

enum class SourceFilterType { ColorCorrection };

struct SourceFilter {
    QString id;
    SourceFilterType type = SourceFilterType::ColorCorrection;
    bool enabled = true;
    float brightness = 0;
    float contrast = 1;
    float saturation = 1;

    bool isIdentity() const { return brightness == 0 && contrast == 1 && saturation == 1; }
    bool valid() const
    {
        return type == SourceFilterType::ColorCorrection
            && std::isfinite(brightness) && brightness >= -1 && brightness <= 1
            && std::isfinite(contrast) && contrast >= 0 && contrast <= 2
            && std::isfinite(saturation) && saturation >= 0 && saturation <= 2;
    }
};
Q_DECLARE_METATYPE(SourceFilter)
Q_DECLARE_METATYPE(QList<SourceFilter>)

#endif
