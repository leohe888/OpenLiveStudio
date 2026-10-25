#include "mainwindow.h"
#include "thememanager.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
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
