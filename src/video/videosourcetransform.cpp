#include "video/videosourcetransform.h"

#include <QRectF>
#include <cmath>

VideoSourceTransform::VideoSourceTransform() = default;

VideoSourceTransform::VideoSourceTransform(const QSizeF &sourceSize)
{
    if (sourceSize.isEmpty())
        return;
    const qreal scale = qMin(1.0, qMin(1920.0 / sourceSize.width(), 1080.0 / sourceSize.height()));
    m_size = sourceSize * scale;
}

VideoSourceTransform::VideoSourceTransform(const QPointF &center, const QSizeF &size, qreal rotation)
    : m_center(center), m_size(size)
{
    setRotation(rotation);
}

bool VideoSourceTransform::isValid() const
{
    return std::isfinite(m_center.x()) && std::isfinite(m_center.y()) &&
           std::isfinite(m_size.width()) && std::isfinite(m_size.height()) &&
           std::isfinite(m_rotation) && m_size.width() > 0 && m_size.height() > 0;
}

QPointF VideoSourceTransform::center() const
{
    return m_center;
}

void VideoSourceTransform::setCenter(const QPointF &center)
{
    m_center = center;
}

QSizeF VideoSourceTransform::size() const
{
    return m_size;
}

void VideoSourceTransform::setSize(const QSizeF &size)
{
    m_size = size;
}

qreal VideoSourceTransform::rotation() const
{
    return m_rotation;
}

void VideoSourceTransform::setRotation(qreal rotation)
{
    m_rotation = std::isfinite(rotation) ? std::fmod(rotation, 360.0) : rotation;
    if (m_rotation < 0)
        m_rotation += 360;
}

QTransform VideoSourceTransform::sourceToCanvas() const
{
    QTransform matrix;
    matrix.translate(m_center.x(), m_center.y());
    matrix.rotate(m_rotation);
    matrix.translate(-m_size.width() / 2, -m_size.height() / 2);
    return matrix;
}

bool VideoSourceTransform::contains(const QPointF &canvasPoint) const
{
    return isValid() &&
           QRectF(QPointF(), m_size).contains(sourceToCanvas().inverted().map(canvasPoint));
}

bool VideoSourceTransform::operator==(const VideoSourceTransform &other) const
{
    return m_center == other.m_center && m_size == other.m_size && m_rotation == other.m_rotation;
}

bool VideoSourceTransform::operator!=(const VideoSourceTransform &other) const
{
    return !(*this == other);
}
