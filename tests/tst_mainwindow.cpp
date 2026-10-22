#include "mainwindow.h"

#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QtTest/QTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void canShowAndClose();
    void hasWindowShell();
    void keepsOutputStatusVisibleWhenResized();
    void loadsScenePanelWithEmptyState();
    void embedsScenePanelInLeftDock();
    void sceneDockCanFloatRedockAndClose();
    void sceneDockShowsLoadErrorAndRecovers();
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

void MainWindowTest::hasWindowShell()
{
    MainWindow window;
    window.show();

    QVERIFY(window.centralWidget());
    QVERIFY(window.centralWidget()->isVisible());
    QVERIFY(window.menuBar()->isVisible());
    QCOMPARE(window.menuBar()->actions().size(), 2);
    auto *toolBar = window.findChild<QToolBar *>(QStringLiteral("mainToolBar"));
    QVERIFY(toolBar);
    QVERIFY(toolBar->isVisible());
    QCOMPARE(window.toolBarArea(toolBar), Qt::TopToolBarArea);
    QVERIFY(window.statusBar()->isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("previewPlaceholder")));
}

void MainWindowTest::keepsOutputStatusVisibleWhenResized()
{
    MainWindow window;
    auto *output = window.findChild<QLabel *>(QStringLiteral("outputStatus"));
    auto *metrics = window.findChild<QLabel *>(QStringLiteral("outputMetrics"));
    QVERIFY(output);
    QVERIFY(metrics);
    window.show();

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        window.statusBar()->showMessage(QStringLiteral("Temporary status message"));
        QTRY_VERIFY(output->isVisible());
        QTRY_VERIFY(output->width() >= output->sizeHint().width());
        const QPoint position = output->mapTo(window.statusBar(), QPoint(0, 0));
        QVERIFY(window.statusBar()->rect().contains(QRect(position, output->size())));
        if (metrics->isVisible())
            QVERIFY(!output->geometry().intersects(metrics->geometry()));
    }
}

void MainWindowTest::loadsScenePanelWithEmptyState()
{
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    panel.setSource(QUrl(QStringLiteral(
        "qrc:/OpenLiveStudio/Ui/qml/ScenesPanel.qml")));

    QStringList errors;
    for (const auto &error : panel.errors())
        errors.append(error.toString());
    QTRY_VERIFY2(panel.status() == QQuickWidget::Ready, qPrintable(errors.join('\n')));
    auto *root = panel.rootObject();
    QVERIFY(root);
    panel.show();

    auto *list = root->findChild<QObject *>(QStringLiteral("sceneList"));
    auto *emptyLabel = root->findChild<QQuickItem *>(QStringLiteral("emptyScenesLabel"));
    QVERIFY(list);
    QVERIFY(emptyLabel);
    QCOMPARE(list->property("count").toInt(), 0);
    QCOMPARE(list->property("currentIndex").toInt(), -1);
    QTRY_VERIFY(emptyLabel->isVisible());

    auto *actions = root->findChild<QQuickItem *>(QStringLiteral("sceneActions"));
    QVERIFY(actions);
    for (const auto *name : {"addSceneButton", "removeSceneButton",
                             "moveSceneUpButton", "moveSceneDownButton"}) {
        // Repeater delegates are children of the visual parent, not necessarily
        // descendants in the QObject ownership tree.
        QQuickItem *button = nullptr;
        for (auto *item : actions->childItems()) {
            if (item->objectName() == QLatin1String(name)) {
                button = item;
                break;
            }
        }
        QVERIFY2(button, name);
        QVERIFY(button->isVisible());
        QVERIFY(!button->isEnabled());
    }

    panel.resize(200, 180);
    QTRY_COMPARE(root->size(), QSizeF(panel.size()));
    QVERIFY(emptyLabel->isVisible());
}

void MainWindowTest::embedsScenePanelInLeftDock()
{
    MainWindow window;
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    QVERIFY(dock);
    QVERIFY(view);
    QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(view->isVisible());
}

void MainWindowTest::sceneDockCanFloatRedockAndClose()
{
    MainWindow window;
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    QVERIFY(dock);
    QVERIFY(view);

    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QVERIFY(view->isVisible());
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);

    QVERIFY(dock->close());
    QTRY_VERIFY(!dock->isVisible());
    dock->show();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(view->isVisible());
}

void MainWindowTest::sceneDockShowsLoadErrorAndRecovers()
{
    MainWindow window;
    window.show();
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    auto *errorLabel = window.findChild<QLabel *>(QStringLiteral("scenesLoadError"));
    QVERIFY(view);
    QVERIFY(errorLabel);

    view->setSource(QUrl(QStringLiteral("qrc:/missing-scenes-panel.qml")));
    QTRY_COMPARE(view->status(), QQuickWidget::Error);
    QTRY_VERIFY(errorLabel->isVisible());
    QVERIFY(!view->isVisible());
    QVERIFY(!errorLabel->toolTip().isEmpty());

    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ScenesPanel.qml")));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QTRY_VERIFY(view->isVisible());
    QVERIFY(!errorLabel->isVisible());
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
