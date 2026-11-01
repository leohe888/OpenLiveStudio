#ifndef VIDEOSOURCETRANSFORM_H
#define VIDEOSOURCETRANSFORM_H

#include <QMetaType>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>
#include <array>
#include <cmath>

struct VideoSourceTransform
{
    QPointF center{960, 540};
    QSizeF size;
    qreal rotation = 0;

    bool operator==(const VideoSourceTransform &other) const
    {
        return center == other.center && size == other.size && rotation == other.rotation;
    }

    bool operator!=(const VideoSourceTransform &other) const
    {
        return !(*this == other);
    }
};
Q_DECLARE_METATYPE(VideoSourceTransform)

namespace VideoSourceGeometry {
inline bool isValid(const VideoSourceTransform &t)
{
    return std::isfinite(t.center.x()) && std::isfinite(t.center.y()) &&
           std::isfinite(t.size.width()) && std::isfinite(t.size.height()) &&
           std::isfinite(t.rotation) && t.size.width() > 0 && t.size.height() > 0;
}

inline qreal normalizedAngle(qreal angle)
{
    const qreal value = std::fmod(angle, 360.0);
    return value < 0 ? value + 360 : value;
}

inline VideoSourceTransform initial(const QSizeF &imageSize)
{
    VideoSourceTransform result;
    if (imageSize.isEmpty())
        return result;
    const qreal scale = qMin(1.0, qMin(1920.0 / imageSize.width(), 1080.0 / imageSize.height()));
    result.size = imageSize * scale;
    return result;
}

inline QTransform sourceToCanvas(const VideoSourceTransform &t)
{
    QTransform matrix;
    matrix.translate(t.center.x(), t.center.y());
    matrix.rotate(t.rotation);
    matrix.translate(-t.size.width() / 2, -t.size.height() / 2);
    return matrix;
}

inline QRectF canvasRect(const QSizeF &widgetSize)
{
    if (widgetSize.isEmpty())
        return {};
    const qreal scale = qMin(widgetSize.width() / 1920, widgetSize.height() / 1080);
    const QSizeF size(1920 * scale, 1080 * scale);
    return QRectF(
        QPointF((widgetSize.width() - size.width()) / 2, (widgetSize.height() - size.height()) / 2),
        size);
}

inline QTransform canvasToWidget(const QSizeF &widgetSize)
{
    const QRectF rect = canvasRect(widgetSize);
    QTransform matrix;
    matrix.translate(rect.x(), rect.y());
    matrix.scale(rect.width() / 1920, rect.height() / 1080);
    return matrix;
}

inline bool contains(const VideoSourceTransform &t, const QPointF &canvasPoint)
{
    return isValid(t) &&
           QRectF(QPointF(), t.size).contains(sourceToCanvas(t).inverted().map(canvasPoint));
}

// Clockwise from the upper-left corner: corners alternate with edge midpoints.
inline const std::array<QPointF, 8> handleFractions{
    {{0, 0}, {.5, 0}, {1, 0}, {1, .5}, {1, 1}, {.5, 1}, {0, 1}, {0, .5}}};

inline VideoSourceTransform resized(const VideoSourceTransform &start, int handle, const QPointF &canvasDelta)
{
    if (!isValid(start) || handle < 0 || handle >= 8)
        return start;
    QTransform rotation;
    rotation.rotate(start.rotation);
    const QPointF delta = rotation.inverted().map(canvasDelta);
    const QPointF fraction = handleFractions[handle];
    const QPointF opposite(1 - fraction.x(), 1 - fraction.y());
    VideoSourceTransform result = start;
    const qreal w = start.size.width(), h = start.size.height();
    if (handle % 2 == 0) {
        const QPointF diagonal((2 * fraction.x() - 1) * w, (2 * fraction.y() - 1) * h);
        const qreal factor =
            qMax(qMax(1 / w, 1 / h), QPointF::dotProduct(diagonal + delta, diagonal) /
                                         QPointF::dotProduct(diagonal, diagonal));
        result.size = start.size * factor;
    } else if (fraction.x() != .5) {
        result.size.setWidth(qMax(1.0, w + (2 * fraction.x() - 1) * delta.x()));
    } else {
        result.size.setHeight(qMax(1.0, h + (2 * fraction.y() - 1) * delta.y()));
    }
    const QPointF anchor = sourceToCanvas(start).map(QPointF(opposite.x() * w, opposite.y() * h));
    result.center = anchor + rotation.map(QPointF((.5 - opposite.x()) * result.size.width(),
                                                  (.5 - opposite.y()) * result.size.height()));
    return result;
}
}

#endif
