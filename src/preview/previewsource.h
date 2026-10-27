#ifndef PREVIEWSOURCE_H
#define PREVIEWSOURCE_H

#include <QImage>
#include <QList>
#include <QString>

struct PreviewSource {
    QString id;
    QImage image;
    bool visible = true;
};

#endif
