#include "mainwindow.h"

#include <QtTest/QTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void canShowAndClose();
};

void MainWindowTest::canShowAndClose()
{
    MainWindow window;
    QVERIFY(!window.isVisible());

    window.show();
    QTRY_VERIFY(window.isVisible());

    QVERIFY(window.close());
    QTRY_VERIFY(!window.isVisible());
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
