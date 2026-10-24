#include "mainwindow.h"

#include <QApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(0);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QApplication a(argc, argv);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
