#ifndef PREVIEWSOURCE_H
#define PREVIEWSOURCE_H

#include "../core/sourcetransform.h"

#include <QImage>
#include <QList>
#include <QString>

struct PreviewSource {
    QString id;
    QImage image;
    bool visible = true;
    SourceTransform transform;
    bool locked = false;
};

#endif
