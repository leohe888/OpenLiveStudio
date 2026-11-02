#ifndef VIDEOSOURCECANVASGEOMETRY_H
#define VIDEOSOURCECANVASGEOMETRY_H

#include <QMetaType>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class VideoSourceCanvasGeometry
{
public:
    VideoSourceCanvasGeometry();
    explicit VideoSourceCanvasGeometry(const QSizeF &pixelSize);
    VideoSourceCanvasGeometry(const QPointF &center, const QSizeF &size);
    VideoSourceCanvasGeometry(const QPointF &center, const QSizeF &size, qreal rotation);

    QPointF center() const;
    void setCenter(const QPointF &center);
    QSizeF size() const;
    void setSize(const QSizeF &size);
    qreal rotation() const;
    void setRotation(qreal rotation);

    bool isValid() const;
    // Maps source-local coordinates to canvas coordinates.
    QTransform sourceToCanvas() const;
    // True when a canvas point falls inside the source rectangle.
    bool contains(const QPointF &canvasPoint) const;
    bool operator==(const VideoSourceCanvasGeometry &other) const;
    bool operator!=(const VideoSourceCanvasGeometry &other) const;

private:
    QPointF m_center{960, 540};
    QSizeF m_size;
    qreal m_rotation = 0;
};
Q_DECLARE_METATYPE(VideoSourceCanvasGeometry)

#endif
