#include "video/videosourcetransform.h"

#include <QRectF>
#include <cmath>

VideoSourceTransform::VideoSourceTransform() = default;

VideoSourceTransform::VideoSourceTransform(const QSizeF &sourceSize)
{
    if (sourceSize.isEmpty())
        return;
    const qreal scale = qMin(1.0, qMin(1920.0 / sourceSize.width(), 1080.0 / sourceSize.height()));
    size = sourceSize * scale;
}

VideoSourceTransform::VideoSourceTransform(const QPointF &center, const QSizeF &size, qreal rotation)
    : center(center), size(size), rotation(rotation)
{
}

bool VideoSourceTransform::isValid() const
{
    return std::isfinite(center.x()) && std::isfinite(center.y()) &&
           std::isfinite(size.width()) && std::isfinite(size.height()) &&
           std::isfinite(rotation) && size.width() > 0 && size.height() > 0;
}

qreal VideoSourceTransform::normalizedAngle(qreal angle)
{
    const qreal value = std::fmod(angle, 360.0);
    return value < 0 ? value + 360 : value;
}

QTransform VideoSourceTransform::sourceToCanvas() const
{
    QTransform matrix;
    matrix.translate(center.x(), center.y());
    matrix.rotate(rotation);
    matrix.translate(-size.width() / 2, -size.height() / 2);
    return matrix;
}

bool VideoSourceTransform::contains(const QPointF &canvasPoint) const
{
    return isValid() &&
           QRectF(QPointF(), size).contains(sourceToCanvas().inverted().map(canvasPoint));
}

bool VideoSourceTransform::operator==(const VideoSourceTransform &other) const
{
    return center == other.center && size == other.size && rotation == other.rotation;
}

bool VideoSourceTransform::operator!=(const VideoSourceTransform &other) const
{
    return !(*this == other);
}
