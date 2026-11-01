#ifndef VIDEOSOURCETRANSFORM_H
#define VIDEOSOURCETRANSFORM_H

#include <QMetaType>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

class VideoSourceTransform
{
public:
    VideoSourceTransform();
    explicit VideoSourceTransform(const QSizeF &sourceSize);
    VideoSourceTransform(const QPointF &center, const QSizeF &size, qreal rotation = 0);

    QPointF center() const;
    void setCenter(const QPointF &center);
    QSizeF size() const;
    void setSize(const QSizeF &size);
    qreal rotation() const;
    void setRotation(qreal rotation);

    bool isValid() const;
    QTransform sourceToCanvas() const;
    bool contains(const QPointF &canvasPoint) const;
    bool operator==(const VideoSourceTransform &other) const;
    bool operator!=(const VideoSourceTransform &other) const;

private:
    QPointF m_center{960, 540};
    QSizeF m_size;
    qreal m_rotation = 0;
};
Q_DECLARE_METATYPE(VideoSourceTransform)

#endif
