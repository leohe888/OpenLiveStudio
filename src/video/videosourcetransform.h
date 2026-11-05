#ifndef VIDEOSOURCETRANSFORM_H
#define VIDEOSOURCETRANSFORM_H

#include "graphics/videoformat.h"

#include <QMetaType>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class VideoSourceTransform
{
public:
    VideoSourceTransform();
    explicit VideoSourceTransform(const QSizeF &pixelSize);
    VideoSourceTransform(const QPointF &center, const QSizeF &size);
    VideoSourceTransform(const QPointF &center, const QSizeF &size, qreal rotationDegrees);

    QPointF center() const;
    void setCenter(const QPointF &center);
    QSizeF size() const;
    void setSize(const QSizeF &size);
    qreal rotationDegrees() const;
    void setRotationDegrees(qreal rotationDegrees);

    bool isValid() const;
    // 将来源局部坐标映射到画布坐标。
    QTransform sourceToCanvas() const;

    bool contains(const QPointF &canvasPoint) const;
    bool operator==(const VideoSourceTransform &other) const;
    bool operator!=(const VideoSourceTransform &other) const;

private:
    QPointF m_center{VideoFormat::width / 2.0, VideoFormat::height / 2.0};
    QSizeF m_size;
    qreal m_rotationDegrees = 0;
};
Q_DECLARE_METATYPE(VideoSourceTransform)

#endif
