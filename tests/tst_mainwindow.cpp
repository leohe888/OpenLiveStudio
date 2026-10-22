#include "mainwindow.h"

#include <QAction>
#include <QDockWidget>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStatusBar>
#include <QToolBar>
#include <QtTest/QTest>
#include <memory>

class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void canShowAndClose();
    void hasWindowShell();
    void keepsOutputStatusVisibleWhenResized();
    void loadsScenePanelWithEmptyState();
    void loadsSourcePanelWithEmptyState();
    void loadsMixerPanelWithUnavailableChannels();
    void loadsTransitionsPanelWithDisabledControls();
    void loadsControlsPanelWithVerticalButtons();
    void embedsScenePanelInLeftDock();
    void embedsSourcePanelBelowSceneDock();
    void embedsMixerPanelInRightDock();
    void embedsTransitionsPanelBelowMixerDock();
    void embedsControlsPanelAcrossBottom();
    void panelDockCanFloatRedockAndClose_data();
    void panelDockCanFloatRedockAndClose();
    void viewMenuCanRestoreAllHiddenDocks();
    void resetLayoutRestoresDocksWithoutReload_data();
    void resetLayoutRestoresDocksWithoutReload();
    void panelDockShowsLoadErrorAndRecovers_data();
    void panelDockShowsLoadErrorAndRecovers();
    void panelIconsCanBeDecoded_data();
    void panelIconsCanBeDecoded();
    void panelIconsRenderInDisabledAndEnabledStates_data();
    void panelIconsRenderInDisabledAndEnabledStates();
};

void MainWindowTest::init()
{
    // A ready QML root can still have unstable layout bindings.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected")));
}

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

void MainWindowTest::loadsMixerPanelWithUnavailableChannels()
{
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml")));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject();
    QVERIFY(root);
    auto *channels = root->findChild<QQuickItem *>(QStringLiteral("mixerChannels"));
    auto *scroll = root->findChild<QQuickItem *>(QStringLiteral("mixerScrollView"));
    QVERIFY(channels);
    QVERIFY(scroll);
    QList<QQuickItem *> channelItems;
    for (auto *item : channels->childItems()) {
        if (item->objectName().endsWith(QStringLiteral("Channel")))
            channelItems.append(item);
    }
    QCOMPARE(channelItems.size(), 2);
    for (auto *channel : channelItems) {
        QQuickItem *slider = nullptr;
        // Repeater-created level tracks belong to the visual tree.
        int tracks = 0;
        const auto inspect = [&](auto &&self, QQuickItem *parent) -> void {
            for (auto *item : parent->childItems()) {
                if (item->objectName() == QStringLiteral("levelTrack"))
                    ++tracks;
                if (item->objectName().endsWith(QStringLiteral("VolumeSlider")))
                    slider = item;
                self(self, item);
            }
        };
        inspect(inspect, channel);
        QCOMPARE(tracks, 2);
        QVERIFY(slider);
        QVERIFY(slider->objectName().endsWith(QStringLiteral("VolumeSlider")));
        QVERIFY(!slider->isEnabled());
        const qreal value = slider->property("value").toReal();
        const QPoint center = slider->mapToScene(slider->boundingRect().center()).toPoint();
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, center);
        QCOMPARE(slider->property("value").toReal(), value);
        auto *status = channel->findChild<QObject *>(QStringLiteral("channelStatus"));
        auto *displayValue = channel->findChild<QObject *>(QStringLiteral("channelValue"));
        QVERIFY(status);
        QVERIFY(displayValue);
        QVERIFY(!status->property("text").toString().isEmpty());
        QCOMPARE(displayValue->property("text").toString(), QString::fromUtf8("—"));
    }
    for (const auto &size : {QSize(160, 140), QSize(400, 500), QSize(238, 260)}) {
        panel.resize(size);
        QTRY_COMPARE(root->size(), QSizeF(size));
        QVERIFY(scroll->property("contentWidth").toReal() >= scroll->width());
        QVERIFY(!channelItems[0]->mapRectToScene(channelItems[0]->boundingRect()).intersects(
            channelItems[1]->mapRectToScene(channelItems[1]->boundingRect())));
    }
    // Short panels retain a scrollable channel layout instead of clipping controls.
    panel.resize(160, 140);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
}

void MainWindowTest::loadsTransitionsPanelWithDisabledControls()
{
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 124);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/TransitionsPanel.qml")));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject();
    QVERIFY(root);
    auto *effect = root->findChild<QQuickItem *>(QStringLiteral("transitionEffect"));
    auto *duration = root->findChild<QQuickItem *>(QStringLiteral("transitionDuration"));
    auto *button = root->findChild<QQuickItem *>(QStringLiteral("executeTransitionButton"));
    QVERIFY(effect);
    QVERIFY(duration);
    QVERIFY(button);
    QCOMPARE(effect->property("count").toInt(), 4);
    QCOMPARE(effect->property("currentIndex").toInt(), -1);
    QCOMPARE(duration->property("value").toInt(), 300);
    QSignalSpy clicked(button, SIGNAL(clicked()));
    QVERIFY(clicked.isValid());
    for (auto *control : {effect, duration, button}) {
        QVERIFY(!control->isEnabled());
        const QPoint center = control->mapToScene(control->boundingRect().center()).toPoint();
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier, center);
        QTest::keyClick(&panel, Qt::Key_Tab);
        QTest::keyClick(&panel, Qt::Key_Up);
        QTest::keyClick(&panel, Qt::Key_Space);
        QVERIFY(!control->hasActiveFocus());
    }
    QCOMPARE(clicked.count(), 0);
    QCOMPARE(effect->property("currentIndex").toInt(), -1);
    QCOMPARE(duration->property("value").toInt(), 300);
    auto *popup = effect->property("popup").value<QObject *>();
    QVERIFY(popup);
    QVERIFY(!popup->property("visible").toBool());

    for (const auto &size : {QSize(180, 124), QSize(400, 240), QSize(238, 124)}) {
        panel.resize(size);
        QTRY_COMPARE(root->size(), QSizeF(size));
        QRectF previous;
        for (auto *control : {effect, duration, button}) {
            const QRectF rect = control->mapRectToScene(control->boundingRect());
            QVERIFY(root->boundingRect().contains(rect));
            QVERIFY(!rect.intersects(previous));
            previous = rect;
        }
    }
}

void MainWindowTest::loadsControlsPanelWithVerticalButtons()
{
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(286, 174);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ControlsPanel.qml")));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject();
    QVERIFY(root);
    auto *scroll = root->findChild<QQuickItem *>(QStringLiteral("controlsScrollView"));
    auto *actions = root->findChild<QQuickItem *>(QStringLiteral("controlActions"));
    QVERIFY(scroll);
    QVERIFY(actions);
    QVERIFY(!root->findChild<QObject *>(QStringLiteral("exitButton")));
    QVERIFY(!root->findChild<QObject *>(QStringLiteral("recordDuration")));
    QVERIFY(!root->findChild<QObject *>(QStringLiteral("streamDuration")));
    QCOMPARE(root->metaObject()->indexOfSignal("exitRequested()"), -1);
    QList<QQuickItem *> buttons;
    for (const auto *name : {"recordButton", "streamButton", "settingsButton"}) {
        auto *button = root->findChild<QQuickItem *>(QLatin1String(name));
        QVERIFY(button);
        QVERIFY(!button->isEnabled());
        QVERIFY(!button->property("checkable").toBool());
        QVERIFY(!button->property("autoExclusive").toBool());
        QSignalSpy clicked(button, SIGNAL(clicked()));
        QVERIFY(clicked.isValid());
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
        QTest::keyClick(&panel, Qt::Key_Tab);
        QVERIFY(!button->hasActiveFocus());
        QCOMPARE(clicked.count(), 0);
        buttons.append(button);
    }
    QCOMPARE(buttons.size(), 3);
    for (const auto &size : {QSize(180, 124), QSize(286, 174), QSize(400, 320)}) {
        panel.resize(size);
        QTRY_COMPARE(root->size(), QSizeF(size));
        QVERIFY(scroll->property("contentWidth").toReal() >= scroll->width());
        QRectF previous;
        for (auto *button : buttons) {
            const QRectF rect = button->mapRectToScene(button->boundingRect());
            QVERIFY(root->boundingRect().contains(rect));
            if (!previous.isNull()) {
                QVERIFY(previous.bottom() < rect.top());
                QCOMPARE(rect.left(), previous.left());
                QCOMPARE(rect.width(), previous.width());
                QVERIFY(qAbs(rect.height() - previous.height()) <= 1);
            }
            previous = rect;
        }
    }
    panel.resize(180, 80);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
    auto *flickable = scroll->property("contentItem").value<QQuickItem *>();
    QVERIFY(flickable);
    QVERIFY(flickable->setProperty("contentY",
        flickable->property("contentHeight").toReal() - flickable->height()));
    QTRY_VERIFY(root->boundingRect().contains(
        buttons.last()->mapRectToScene(buttons.last()->boundingRect())));
    QVERIFY(panel.isVisible());
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

void MainWindowTest::embedsSourcePanelBelowSceneDock()
{
    MainWindow window;
    window.show();
    auto *scenesDock = window.findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("sourcesQuickWidget"));
    QVERIFY(scenesDock);
    QVERIFY(dock);
    QVERIFY(view);
    QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    auto *list = view->rootObject()->findChild<QObject *>(QStringLiteral("sourceList"));
    QVERIFY(list);
    QCOMPARE(list->property("count").toInt(), 0);

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(view->isVisible());
        QVERIFY(scenesDock->isVisible());
        QTRY_VERIFY(scenesDock->geometry().bottom() < dock->geometry().top());
        QVERIFY(!dock->geometry().intersects(window.centralWidget()->geometry()));
        QTRY_COMPARE(view->rootObject()->size(), QSizeF(view->size()));
        auto *actions = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceActions"));
        QVERIFY(actions);
        for (auto *button : actions->childItems()) {
            if (!button->objectName().endsWith(QStringLiteral("Button")))
                continue;
            QVERIFY(!button->isEnabled());
            QVERIFY(view->rootObject()->boundingRect().contains(
                button->mapRectToScene(button->boundingRect())));
        }
    }
}

void MainWindowTest::embedsMixerPanelInRightDock()
{
    MainWindow window;
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("mixerDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("mixerQuickWidget"));
    auto *errorLabel = window.findChild<QLabel *>(QStringLiteral("mixerLoadError"));
    QVERIFY(dock);
    QVERIFY(view);
    QVERIFY(errorLabel);
    QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    QCOMPARE(view->rootObject()->objectName(), QStringLiteral("mixerPanel"));

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(view->isVisible());
        QVERIFY(!errorLabel->isVisible());
        QTRY_COMPARE(view->rootObject()->size(), QSizeF(view->size()));
        QTRY_VERIFY(dock->geometry().left() > window.centralWidget()->geometry().right());
        QVERIFY(window.centralWidget()->width() > 0);
        QVERIFY(window.rect().contains(dock->geometry()));
    }
}

void MainWindowTest::embedsTransitionsPanelBelowMixerDock()
{
    MainWindow window;
    window.show();
    auto *mixerDock = window.findChild<QDockWidget *>(QStringLiteral("mixerDock"));
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("transitionsDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("transitionsQuickWidget"));
    auto *errorLabel = window.findChild<QLabel *>(QStringLiteral("transitionsLoadError"));
    QVERIFY(mixerDock);
    QVERIFY(dock);
    QVERIFY(view);
    QVERIFY(errorLabel);
    QCOMPARE(window.dockWidgetArea(dock), Qt::RightDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    QCOMPARE(view->rootObject()->objectName(), QStringLiteral("transitionsPanel"));

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(view->isVisible());
        QVERIFY(mixerDock->isVisible());
        QVERIFY(!errorLabel->isVisible());
        QTRY_VERIFY(mixerDock->geometry().bottom() < dock->geometry().top());
        QVERIFY(dock->geometry().left() > window.centralWidget()->geometry().right());
        QVERIFY(window.rect().contains(dock->geometry()));
        QTRY_COMPARE(view->rootObject()->size(), QSizeF(view->size()));
        for (const auto *name : {"transitionEffect", "transitionDuration", "executeTransitionButton"}) {
            auto *control = view->rootObject()->findChild<QQuickItem *>(QLatin1String(name));
            QVERIFY(control);
            QVERIFY(!control->isEnabled());
            QVERIFY(view->rootObject()->boundingRect().contains(
                control->mapRectToScene(control->boundingRect())));
        }
    }
}

void MainWindowTest::embedsControlsPanelAcrossBottom()
{
    MainWindow window;
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("controlsDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("controlsQuickWidget"));
    QVERIFY(dock);
    QVERIFY(view);
    QCOMPARE(window.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    QCOMPARE(view->rootObject()->objectName(), QStringLiteral("controlsPanel"));

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        QTRY_VERIFY(view->isVisible());
        QTRY_COMPARE(view->rootObject()->size(), QSizeF(view->size()));
        const QRect barRect(window.statusBar()->mapTo(&window, QPoint()), window.statusBar()->size());
        QVERIFY(dock->geometry().bottom() < barRect.top());
        QVERIFY(window.centralWidget()->geometry().bottom() < dock->geometry().top());
        for (auto *otherDock : window.findChildren<QDockWidget *>()) {
            if (otherDock == dock)
                continue;
            QVERIFY(otherDock->isVisible());
            QVERIFY(otherDock->geometry().bottom() < dock->geometry().top());
            QVERIFY(dock->geometry().left() <= otherDock->geometry().left());
            QVERIFY(dock->geometry().right() >= otherDock->geometry().right());
        }
        for (const auto *name : {"recordButton", "streamButton", "settingsButton"}) {
            auto *button = view->rootObject()->findChild<QQuickItem *>(QLatin1String(name));
            QVERIFY(button);
            QVERIFY(!button->isEnabled());
            QVERIFY(view->rootObject()->boundingRect().contains(
                button->mapRectToScene(button->boundingRect())));
        }
    }
}

void MainWindowTest::panelDockCanFloatRedockAndClose_data()
{
    QTest::addColumn<QString>("panelId");
    QTest::addColumn<Qt::DockWidgetArea>("dockArea");
    QTest::addColumn<QString>("qmlName");
    QTest::newRow("scenes") << QStringLiteral("scenes")
        << Qt::LeftDockWidgetArea << QStringLiteral("ScenesPanel");
    QTest::newRow("sources") << QStringLiteral("sources")
        << Qt::LeftDockWidgetArea << QStringLiteral("SourcesPanel");
    QTest::newRow("mixer") << QStringLiteral("mixer")
        << Qt::RightDockWidgetArea << QStringLiteral("MixerPanel");
    QTest::newRow("transitions") << QStringLiteral("transitions")
        << Qt::RightDockWidgetArea << QStringLiteral("TransitionsPanel");
    QTest::newRow("controls") << QStringLiteral("controls")
        << Qt::BottomDockWidgetArea << QStringLiteral("ControlsPanel");
}

void MainWindowTest::panelDockCanFloatRedockAndClose()
{
    QFETCH(QString, panelId);
    QFETCH(Qt::DockWidgetArea, dockArea);
    MainWindow window;
    window.show();
    auto *dock = window.findChild<QDockWidget *>(panelId + QStringLiteral("Dock"));
    auto *view = window.findChild<QQuickWidget *>(panelId + QStringLiteral("QuickWidget"));
    QVERIFY(dock);
    QVERIFY(view);

    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    auto *action = dock->toggleViewAction();
    QVERIFY(viewMenu);
    QVERIFY(viewMenu->actions().contains(action));
    QVERIFY(action->isEnabled());
    QVERIFY(action->isCheckable());
    QTRY_VERIFY(action->isChecked());
    const QPointer<QQuickItem> originalRoot = view->rootObject();
    QVERIFY(originalRoot);
    action->trigger();
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!action->isChecked());
    action->trigger();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(action->isChecked());

    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QVERIFY(view->isVisible());
    QTRY_VERIFY(action->isChecked());
    QVERIFY(dock->close());
    QTRY_VERIFY(!action->isChecked());
    action->trigger();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(action->isChecked());
    QVERIFY(dock->isFloating());
    dock->setFloating(false);
    QTRY_VERIFY(!dock->isFloating());
    QCOMPARE(window.dockWidgetArea(dock), dockArea);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);

    QVERIFY(dock->close());
    QTRY_VERIFY(!dock->isVisible());
    QVERIFY(!action->isChecked());
    for (auto *otherView : window.findChildren<QQuickWidget *>()) {
        if (otherView == view)
            continue;
        QVERIFY(otherView->isVisible());
        QCOMPARE(otherView->status(), QQuickWidget::Ready);
    }
    action->trigger();
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(view->isVisible());
    QVERIFY(action->isChecked());
    QCOMPARE(view->rootObject(), originalRoot.data());
}

void MainWindowTest::viewMenuCanRestoreAllHiddenDocks()
{
    MainWindow window;
    window.show();
    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    QVERIFY(viewMenu);
    const auto docks = window.findChildren<QDockWidget *>();
    QCOMPARE(docks.size(), 5);
    for (auto *dock : docks) {
        QVERIFY(dock->close());
        QTRY_VERIFY(!dock->isVisible());
        QVERIFY(!dock->toggleViewAction()->isChecked());
    }
    QVERIFY(window.menuBar()->isVisible());
    for (auto *dock : docks) {
        auto *action = dock->toggleViewAction();
        QVERIFY(viewMenu->actions().contains(action));
        QVERIFY(action->isEnabled());
        action->trigger();
        QTRY_VERIFY(dock->isVisible());
        QVERIFY(action->isChecked());
    }
}

void MainWindowTest::resetLayoutRestoresDocksWithoutReload_data()
{
    QTest::addColumn<QSize>("windowSize");
    QTest::newRow("default-size") << QSize(1280, 800);
    QTest::newRow("minimum-size") << QSize(960, 640);
}

void MainWindowTest::resetLayoutRestoresDocksWithoutReload()
{
    QFETCH(QSize, windowSize);
    MainWindow window;
    window.resize(windowSize);
    window.show();
    auto *resetAction = window.findChild<QAction *>(QStringLiteral("resetLayoutAction"));
    QVERIFY(resetAction);
    QVERIFY(resetAction->isEnabled());
    QList<QDockWidget *> docks;
    QList<QQuickWidget *> views;
    QList<QPointer<QQuickItem>> roots;
    const QList<Qt::DockWidgetArea> areas{Qt::LeftDockWidgetArea, Qt::LeftDockWidgetArea,
        Qt::RightDockWidgetArea, Qt::RightDockWidgetArea, Qt::BottomDockWidgetArea};
    for (const auto *id : {"scenes", "sources", "mixer", "transitions", "controls"}) {
        auto *dock = window.findChild<QDockWidget *>(QLatin1String(id) + QStringLiteral("Dock"));
        auto *view = window.findChild<QQuickWidget *>(QLatin1String(id) + QStringLiteral("QuickWidget"));
        QVERIFY(dock);
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        docks.append(dock);
        views.append(view);
        roots.append(view->rootObject());
    }

    // Mix moved, tabbed, floating and hidden panels, then hide every floating panel.
    for (int pass = 0; pass < 3; ++pass) {
        if (pass == 0) {
            window.addDockWidget(Qt::RightDockWidgetArea, docks[1]);
            window.tabifyDockWidget(docks[2], docks[1]);
            QVERIFY(!window.tabifiedDockWidgets(docks[2]).isEmpty());
            docks[3]->setFloating(true);
            docks[3]->move(-10000, -10000);
            docks[0]->close();
            docks[4]->setFloating(true);
            docks[4]->close();
        } else if (pass == 1) {
            for (auto *dock : docks) {
                dock->setFloating(true);
                dock->close();
            }
        }
        resetAction->trigger();
        QTRY_COMPARE(window.size(), windowSize);
        QCOMPARE(window.findChildren<QDockWidget *>().size(), 5);
        QCOMPARE(window.findChildren<QQuickWidget *>().size(), 5);
        for (int i = 0; i < docks.size(); ++i) {
            QVERIFY(!docks[i]->isFloating());
            QTRY_VERIFY(docks[i]->isVisible());
            QVERIFY(docks[i]->toggleViewAction()->isChecked());
            QCOMPARE(window.dockWidgetArea(docks[i]), areas[i]);
            QVERIFY(window.tabifiedDockWidgets(docks[i]).isEmpty());
            QVERIFY(roots[i]);
            QCOMPARE(views[i]->rootObject(), roots[i].data());
            QCOMPARE(views[i]->status(), QQuickWidget::Ready);
        }
        QTRY_VERIFY(docks[0]->geometry().bottom() < docks[1]->geometry().top());
        QTRY_VERIFY(docks[2]->geometry().bottom() < docks[3]->geometry().top());
        for (int i = 0; i < 4; ++i) {
            QVERIFY(docks[i]->geometry().bottom() < docks[4]->geometry().top());
            QVERIFY(docks[4]->geometry().left() <= docks[i]->geometry().left());
            QVERIFY(docks[4]->geometry().right() >= docks[i]->geometry().right());
        }
        QVERIFY(docks[0]->geometry().right() < window.centralWidget()->geometry().left());
        QVERIFY(docks[2]->geometry().left() > window.centralWidget()->geometry().right());
    }
}

void MainWindowTest::panelDockShowsLoadErrorAndRecovers_data()
{
    panelDockCanFloatRedockAndClose_data();
}

void MainWindowTest::panelDockShowsLoadErrorAndRecovers()
{
    QFETCH(QString, panelId);
    QFETCH(QString, qmlName);
    MainWindow window;
    window.show();
    auto *view = window.findChild<QQuickWidget *>(panelId + QStringLiteral("QuickWidget"));
    auto *errorLabel = window.findChild<QLabel *>(panelId + QStringLiteral("LoadError"));
    QVERIFY(view);
    QVERIFY(errorLabel);

    view->setSource(QUrl(QStringLiteral("qrc:/missing-%1-panel.qml").arg(panelId)));
    QTRY_COMPARE(view->status(), QQuickWidget::Error);
    QTRY_VERIFY(errorLabel->isVisible());
    QVERIFY(!view->isVisible());
    QVERIFY(!errorLabel->toolTip().isEmpty());
    for (auto *otherView : window.findChildren<QQuickWidget *>()) {
        if (otherView == view)
            continue;
        QVERIFY(otherView->isVisible());
        QCOMPARE(otherView->status(), QQuickWidget::Ready);
    }
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(qmlName)));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QTRY_VERIFY(view->isVisible());
    QVERIFY(!errorLabel->isVisible());
}

void MainWindowTest::panelIconsCanBeDecoded_data()
{
    QTest::addColumn<QString>("resourcePath");
    for (const auto *name : {"plus", "minus", "arrow-up", "arrow-down",
                             "eye", "lock", "sliders-horizontal", "settings", "volume-2", "arrow-right",
                             "circle", "radio"}) {
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
    QTest::newRow("mixer") << QStringLiteral("MixerPanel") << 4;
    QTest::newRow("transitions") << QStringLiteral("TransitionsPanel") << 1;
    QTest::newRow("controls") << QStringLiteral("ControlsPanel") << 2;
}

void MainWindowTest::panelIconsRenderInDisabledAndEnabledStates()
{
    QFETCH(QString, panelName);
    QFETCH(int, expectedButtons);
    QQuickWidget panel;
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    if (panelName == QStringLiteral("ControlsPanel"))
        panel.resize(286, 174);
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
    if (panelName == QStringLiteral("ControlsPanel")) {
        buttons.removeIf([](QQuickItem *button) {
            return button->objectName() != QStringLiteral("recordButton")
                && button->objectName() != QStringLiteral("streamButton");
        });
    }
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
        // Isolate icons from button text and borders for rendering checks.
        const bool isolateIcon = panelName == QStringLiteral("TransitionsPanel")
            || panelName == QStringLiteral("ControlsPanel");
        const QVariant originalDisplay = button->property("display");
        auto *background = button->property("background").value<QQuickItem *>();
        if (isolateIcon) {
            button->setProperty("display", 0); // AbstractButton.IconOnly
            QVERIFY(background);
            background->setVisible(false);
        }
        QVERIFY(!button->isEnabled());
        QTRY_VERIFY(hasIconPixels(captureButton(button)));
        const QImage disabledImage = captureButton(button);

        // Exercise presentation only; business operations remain unconnected.
        button->setEnabled(true);
        QTRY_VERIFY(captureButton(button) != disabledImage);
        QVERIFY(hasIconPixels(captureButton(button)));
        button->setEnabled(false);
        button->setProperty("display", originalDisplay);
        if (isolateIcon)
            background->setVisible(true);
        ++checkedButtons;
    }
    QCOMPARE(checkedButtons, expectedButtons);
}

QTEST_MAIN(MainWindowTest)
#include "tst_mainwindow.moc"
