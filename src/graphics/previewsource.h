#ifndef PREVIEWSOURCE_H
#define PREVIEWSOURCE_H

#include "video/videosourcetransform.h"
#include "video/videosourcefilter.h"
#include "graphics/captureframe.h"

#include <QImage>
#include <QList>
#include <QString>

struct PreviewSource
{
    QString id;
    QImage image;
    bool visible = true;
    VideoSourceTransform transform;
    bool locked = false;
    QList<VideoSourceFilter> filters;
    std::shared_ptr<const CaptureFrame> frame;

    QSize pixelSize() const
    {
        return frame ? frame->size : image.size();
    }

    bool hasFrame() const
    {
        return frame ? bool(frame->gpu) : !image.isNull();
    }
};

#endif
