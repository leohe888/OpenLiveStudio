#ifndef VIDEOSOURCETRANSFORM_H
#define VIDEOSOURCETRANSFORM_H

#include <QMetaType>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

struct VideoSourceTransform
{
    VideoSourceTransform();
    explicit VideoSourceTransform(const QSizeF &sourceSize);
    VideoSourceTransform(const QPointF &center, const QSizeF &size, qreal rotation = 0);

    QPointF center{960, 540};
    QSizeF size;
    qreal rotation = 0;

    bool isValid() const;
    QTransform sourceToCanvas() const;
    bool contains(const QPointF &canvasPoint) const;
    static qreal normalizedAngle(qreal angle);
    bool operator==(const VideoSourceTransform &other) const;
    bool operator!=(const VideoSourceTransform &other) const;
};
Q_DECLARE_METATYPE(VideoSourceTransform)

#endif
