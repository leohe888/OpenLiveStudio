#include "mainwindow.h"

#include <QDockWidget>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QStatusBar>
#include <QToolBar>
#include <QtTest/QTest>
#include <memory>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void canShowAndClose();
    void hasWindowShell();
    void keepsOutputStatusVisibleWhenResized();
    void loadsScenePanelWithEmptyState();
    void loadsSourcePanelWithEmptyState();
    void embedsScenePanelInLeftDock();
    void sceneDockCanFloatRedockAndClose();
    void sceneDockShowsLoadErrorAndRecovers();
    void panelIconsCanBeDecoded_data();
    void panelIconsCanBeDecoded();
    void panelIconsRenderInDisabledAndEnabledStates_data();
    void panelIconsRenderInDisabledAndEnabledStates();
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

void MainWindowTest::loadsSourcePanelWithEmptyState()
{
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 320);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject();
    QVERIFY(root);
    auto *list = root->findChild<QObject *>(QStringLiteral("sourceList"));
    auto *emptyLabel = root->findChild<QQuickItem *>(QStringLiteral("emptySourcesLabel"));
    auto *actions = root->findChild<QQuickItem *>(QStringLiteral("sourceActions"));
    QVERIFY(list);
    QVERIFY(emptyLabel);
    QVERIFY(actions);
    QCOMPARE(list->property("count").toInt(), 0);
    QCOMPARE(list->property("currentIndex").toInt(), -1);
    QTRY_VERIFY(emptyLabel->isVisible());

    for (const auto &size : {QSize(238, 320), QSize(220, 180), QSize(400, 500)}) {
        panel.resize(size);
        QTRY_COMPARE(root->size(), QSizeF(size));
        int buttons = 0;
        QRectF previousButton;
        for (auto *item : actions->childItems()) {
            if (!item->objectName().endsWith(QStringLiteral("Button")))
                continue;
            QVERIFY(item->isVisible());
            QVERIFY(!item->isEnabled());
            const QRectF rect = item->mapRectToScene(item->boundingRect());
            QVERIFY(root->boundingRect().contains(rect));
            QVERIFY(!rect.intersects(previousButton));
            previousButton = rect;
            ++buttons;
        }
        QCOMPARE(buttons, 6);
        QVERIFY(emptyLabel->isVisible());
    }
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

void MainWindowTest::panelIconsCanBeDecoded_data()
{
    QTest::addColumn<QString>("resourcePath");
    for (const auto *name : {"plus", "minus", "arrow-up", "arrow-down",
                             "eye", "lock", "sliders-horizontal", "settings"}) {
        QTest::newRow(name) << QStringLiteral(":/assets/icons/%1.svg").arg(QLatin1String(name));
    }
}

void MainWindowTest::panelIconsCanBeDecoded()
{
    QFETCH(QString, resourcePath);
    QImageReader reader(resourcePath);
    const QImage image = reader.read();
    QVERIFY2(!image.isNull(), qPrintable(reader.errorString()));
    QCOMPARE(image.size(), QSize(24, 24));
    QVERIFY(image.hasAlphaChannel());
}

void MainWindowTest::panelIconsRenderInDisabledAndEnabledStates_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<int>("expectedButtons");
    QTest::newRow("scenes") << QStringLiteral("ScenesPanel") << 4;
    QTest::newRow("sources") << QStringLiteral("SourcesPanel") << 8;
}

void MainWindowTest::panelIconsRenderInDisabledAndEnabledStates()
{
    QFETCH(QString, panelName);
    QFETCH(int, expectedButtons);
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(panelName)));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    // Test-only data instantiates the source row; production keeps a null model.
    std::unique_ptr<QObject> sourceModel;
    if (panelName == QStringLiteral("SourcesPanel")) {
        QQmlComponent component(panel.engine());
        component.setData("import QtQml.Models\nListModel { ListElement { "
                          "sourceName: 'A long source name for layout verification' } }", QUrl());
        sourceModel.reset(component.create());
        QVERIFY2(sourceModel, qPrintable(component.errorString()));
        auto *list = panel.rootObject()->findChild<QObject *>(QStringLiteral("sourceList"));
        QVERIFY(list);
        QVERIFY(list->setProperty("model", QVariant::fromValue(sourceModel.get())));
        QTRY_COMPARE(list->property("count").toInt(), 1);
        auto *emptyLabel = panel.rootObject()->findChild<QQuickItem *>(QStringLiteral("emptySourcesLabel"));
        QVERIFY(emptyLabel);
        QTRY_VERIFY(!emptyLabel->isVisible());
    }
    QList<QQuickItem *> buttons;
    const auto collectButtons = [&buttons](auto &&self, QQuickItem *parent) -> void {
        for (auto *item : parent->childItems()) {
            if (item->objectName().endsWith(QStringLiteral("Button")))
                buttons.append(item);
            self(self, item);
        }
    };
    collectButtons(collectButtons, panel.rootObject());
    QCOMPARE(buttons.size(), expectedButtons);

    const auto captureButton = [&panel](QQuickItem *button) {
        const QImage frame = panel.grabFramebuffer();
        const QRectF rect = button->mapRectToScene(button->boundingRect());
        const qreal scale = qreal(frame.width()) / panel.width();
        return frame.copy(QRect(qRound(rect.x() * scale), qRound(rect.y() * scale),
                                qRound(rect.width() * scale), qRound(rect.height() * scale)));
    };
    const auto hasIconPixels = [](const QImage &image) {
        if (image.isNull())
            return false;
        const QRgb background = image.pixel(0, 0);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixel(x, y) != background)
                    return true;
            }
        }
        return false;
    };

    int checkedButtons = 0;
    for (auto *button : buttons) {
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY(hasIconPixels(captureButton(button)));
        const QImage disabledImage = captureButton(button);

        // Exercise presentation only; business operations remain unconnected.
        button->setEnabled(true);
        QTRY_VERIFY(captureButton(button) != disabledImage);
        QVERIFY(hasIconPixels(captureButton(button)));
        button->setEnabled(false);
        ++checkedButtons;
    }
    QCOMPARE(checkedButtons, expectedButtons);
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
