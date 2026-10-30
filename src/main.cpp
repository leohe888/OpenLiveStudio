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
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    MainWindow w;
    w.show();
    return QApplication::exec();
}
