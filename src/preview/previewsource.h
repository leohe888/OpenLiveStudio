#ifndef PREVIEWSOURCE_H
#define PREVIEWSOURCE_H

#include "../core/sourcetransform.h"
#include "../core/sourcefilter.h"

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
};

#endif
