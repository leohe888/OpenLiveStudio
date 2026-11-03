#ifndef RENDERSOURCESNAPSHOT_H
#define RENDERSOURCESNAPSHOT_H

#include "video/videosourcetransform.h"
#include "video/videosourcefilter.h"
#include "graphics/captureframe.h"

#include <QImage>
#include <QList>
#include <QString>

// 图片和摄像头使用 CPU 图像，显示器采集使用 GPU 帧；frame 非空时优先采用它。
struct RenderSourceSnapshot
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
