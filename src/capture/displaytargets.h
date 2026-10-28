#ifndef DISPLAYTARGETS_H
#define DISPLAYTARGETS_H
#include <QList>
#include <QSize>
#include <QString>
struct DisplayTarget {
    QString id;
    QString name;
    QSize size;
    quintptr monitor = 0;
};
QList<DisplayTarget> displayTargets();
#endif
