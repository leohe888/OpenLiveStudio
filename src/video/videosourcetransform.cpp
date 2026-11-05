#include "graphics/videoformat.h"
#include "video/videosourcetransform.h"

#include <QRectF>
#include <cmath>

VideoSourceTransform::VideoSourceTransform() = default;

VideoSourceTransform::VideoSourceTransform(const QSizeF &pixelSize)
{
    if (pixelSize.isEmpty())
        return;
    const qreal scale = qMin(1.0, qMin(qreal(VideoFormat::width) / pixelSize.width(),
                                       qreal(VideoFormat::height) / pixelSize.height()));
    m_size = pixelSize * scale;
}

VideoSourceTransform::VideoSourceTransform(const QPointF &center, const QSizeF &size)
    : m_center(center), m_size(size)
{
}

VideoSourceTransform::VideoSourceTransform(const QPointF &center, const QSizeF &size,
                                                     qreal rotationDegrees)
    : m_center(center), m_size(size)
{
    setRotationDegrees(rotationDegrees);
}

bool VideoSourceTransform::isValid() const
{
    return std::isfinite(m_center.x()) && std::isfinite(m_center.y()) &&
           std::isfinite(m_size.width()) && std::isfinite(m_size.height()) &&
           std::isfinite(m_rotationDegrees) && m_size.width() > 0 && m_size.height() > 0;
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

qreal VideoSourceTransform::rotationDegrees() const
{
    return m_rotationDegrees;
}

void VideoSourceTransform::setRotationDegrees(qreal rotationDegrees)
{
    m_rotationDegrees =
        std::isfinite(rotationDegrees) ? std::fmod(rotationDegrees, 360.0) : rotationDegrees;
    if (m_rotationDegrees < 0)
        m_rotationDegrees += 360;
}

QTransform VideoSourceTransform::sourceToCanvas() const
{
    QTransform matrix;
    matrix.translate(m_center.x(), m_center.y());
    matrix.rotate(m_rotationDegrees);
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
    return m_center == other.m_center && m_size == other.m_size &&
           m_rotationDegrees == other.m_rotationDegrees;
}

bool VideoSourceTransform::operator!=(const VideoSourceTransform &other) const
{
    return !(*this == other);
}
