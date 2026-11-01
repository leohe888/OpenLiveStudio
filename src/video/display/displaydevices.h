#ifndef DISPLAYDEVICES_H
#define DISPLAYDEVICES_H
#include <QList>
#include <QSize>
#include <QString>

struct DisplayDevice
{
    QString id;
    QString name;
    QSize size;
    quintptr monitor = 0;

    bool operator==(const DisplayDevice &other) const
    {
        return id == other.id && name == other.name && size == other.size &&
               monitor == other.monitor;
    }
};

QList<DisplayDevice> displayDevices();
#endif
