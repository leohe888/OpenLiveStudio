#include "video/videosourcecanvasgeometry.h"

#include <QRectF>
#include <cmath>

VideoSourceCanvasGeometry::VideoSourceCanvasGeometry() = default;

VideoSourceCanvasGeometry::VideoSourceCanvasGeometry(const QSizeF &pixelSize)
{
    if (pixelSize.isEmpty())
        return;
    const qreal scale = qMin(1.0, qMin(1920.0 / pixelSize.width(), 1080.0 / pixelSize.height()));
    m_size = pixelSize * scale;
}

VideoSourceCanvasGeometry::VideoSourceCanvasGeometry(const QPointF &center, const QSizeF &size, qreal rotation)
    : m_center(center), m_size(size)
{
    setRotation(rotation);
}

bool VideoSourceCanvasGeometry::isValid() const
{
    return std::isfinite(m_center.x()) && std::isfinite(m_center.y()) &&
           std::isfinite(m_size.width()) && std::isfinite(m_size.height()) &&
           std::isfinite(m_rotation) && m_size.width() > 0 && m_size.height() > 0;
}

QPointF VideoSourceCanvasGeometry::center() const
{
    return m_center;
}

void VideoSourceCanvasGeometry::setCenter(const QPointF &center)
{
    m_center = center;
}

QSizeF VideoSourceCanvasGeometry::size() const
{
    return m_size;
}

void VideoSourceCanvasGeometry::setSize(const QSizeF &size)
{
    m_size = size;
}

qreal VideoSourceCanvasGeometry::rotation() const
{
    return m_rotation;
}

void VideoSourceCanvasGeometry::setRotation(qreal rotation)
{
    m_rotation = std::isfinite(rotation) ? std::fmod(rotation, 360.0) : rotation;
    if (m_rotation < 0)
        m_rotation += 360;
}

QTransform VideoSourceCanvasGeometry::sourceToCanvas() const
{
    QTransform matrix;
    matrix.translate(m_center.x(), m_center.y());
    matrix.rotate(m_rotation);
    matrix.translate(-m_size.width() / 2, -m_size.height() / 2);
    return matrix;
}

bool VideoSourceCanvasGeometry::contains(const QPointF &canvasPoint) const
{
    return isValid() &&
           QRectF(QPointF(), m_size).contains(sourceToCanvas().inverted().map(canvasPoint));
}

bool VideoSourceCanvasGeometry::operator==(const VideoSourceCanvasGeometry &other) const
{
    return m_center == other.m_center && m_size == other.m_size && m_rotation == other.m_rotation;
}

bool VideoSourceCanvasGeometry::operator!=(const VideoSourceCanvasGeometry &other) const
{
    return !(*this == other);
}
