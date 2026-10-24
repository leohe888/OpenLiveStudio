#include "mainwindow.h"
#include "thememanager.h"

#include <QApplication>
#include <QMessageBox>
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
    ThemeManager theme;
    if (!theme.isReady()) {
        QMessageBox::critical(nullptr, QObject::tr("主题加载失败"), theme.lastError());
        return 1;
    }
    MainWindow w(theme);
    w.show();
    return QApplication::exec();
}
