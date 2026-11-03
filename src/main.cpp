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

    QApplication application(argc, argv);

    QFile styleFile(":/assets/styles/style.qss");
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("Cannot load application style sheet");
    application.setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    MainWindow mainWindow;
    mainWindow.show();

    return QApplication::exec();
}
