#ifndef DISPLAYTARGETS_H
#define DISPLAYTARGETS_H
#include <QList>
#include <QSize>
#include <QString>

struct DisplayTarget
{
    QString id;
    QString name;
    QSize size;
    quintptr monitor = 0;

    bool operator==(const DisplayTarget &other) const
    {
        return id == other.id && name == other.name && size == other.size &&
               monitor == other.monitor;
    }
};

QList<DisplayTarget> displayTargets();
#endif
