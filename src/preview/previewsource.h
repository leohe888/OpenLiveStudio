#ifndef PREVIEWSOURCE_H
#define PREVIEWSOURCE_H

#include "../core/sourcetransform.h"
#include "../core/sourcefilter.h"
#include "../capture/captureframe.h"

#include <QImage>
#include <QList>
#include <QString>

struct PreviewSource {
    QString id;
    QImage image;
    bool visible = true;
    SourceTransform transform;
    bool locked = false;
    QList<SourceFilter> filters;
    std::shared_ptr<const CaptureFrame> frame;

    QSize pixelSize() const { return frame ? frame->size : image.size(); }
    const QImage &cpuImage() const { return frame ? frame->image : image; }
    bool hasFrame() const { return frame ? (!frame->image.isNull() || bool(frame->gpu)) : !image.isNull(); }
};

#endif
