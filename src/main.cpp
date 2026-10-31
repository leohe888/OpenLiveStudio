#include "ui/mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);

    QFile style(":/assets/styles/style.qss");
    if (!style.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("Cannot load application style sheet");
    a.setStyleSheet(QString::fromUtf8(style.readAll()));

    MainWindow w;
    w.show();

    return QApplication::exec();
}
