#include "mainwindow.h"
#include "audio/audiosourcemodel.h"
#include "output/outputsettings.h"
#include "output/recordingcontroller.h"
#include "thememanager.h"
#include "sourcelistmodel.h"
#include "imagesourcecontroller.h"
#include "capture/cameracapturecontroller.h"
#include <QPainter>
#include <QLineF>
#include <QFileDialog>
#include <QTimer>
#include "preview/previewwidget.h"
#include <QApplication>

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QDockWidget>
#include <QDialog>
#include <QFile>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStatusBar>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWheelEvent>
#include <QtTest/QTest>
#include <memory>
#include "recordingacceptance.h"

namespace {
QQuickItem *findQuickItem(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name) return item;
    for (auto *child : item->childItems()) if (auto *found = findQuickItem(child, name)) return found;
    return nullptr;
}
QImage grabPreview(PreviewWidget &preview)
{
    preview.compositor()->captureImage();
    QCoreApplication::processEvents();
    return preview.grabFramebuffer();
}
}

class MainWindowTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_settings;
    std::unique_ptr<ThemeManager> m_theme;

private slots:
    void init();
    void canShowAndClose();
    void hasWindowShell();
    void previewDisplaysImageSources();
    void previewEditsModelAndSynchronizesSelection();
    void previewShowsRenderingError();
    void dockTitlesStayVerticallyCentered();
    void themeRestoresAndUpdatesOuterWidgets();
    void themeMenuSwitchesAndRestoresPreference();
    void themeMenuRecoversFromSaveFailure();
    void dialogsShareThemeAndReleaseBeforeIt();
    void basicDialogThemes_data();
    void basicDialogThemes();
    void sourceThemes_data();
    void sourceThemes();
    void mixerAndControlThemes_data();
    void mixerAndControlThemes();
    void sourcePanelSharesStateAcrossReloadsAndReleasesItWithWindow();
    void sourcePanelFollowsSelectionAcrossReloads();
    void sourcePanelCanMoveAndToggle_data();
    void sourcePanelCanMoveAndToggle();
    void listScrollBarsHandleOverflow_data();
    void listScrollBarsHandleOverflow();
    void qmlDialogReceivesInitialPropertiesAndCloses();
    void qmlDialogShowsLoadErrors_data();
    void qmlDialogShowsLoadErrors();
    void aboutActionShowsProjectInfoAndReusesDialog();
    void sourcePanelOpensAddDialog_data();
    void sourcePanelOpensAddDialog();
    void addsImageThroughDialog();
    void cancelsPendingImageThroughDialog_data();
    void cancelsPendingImageThroughDialog();
    void sourceAddDialogHandlesInput_data();
    void sourceAddDialogHandlesInput();
    void sourceAddDialogHandlesTypeSelection_data();
    void sourceAddDialogHandlesTypeSelection();
    void sourcePanelRequestsRemoval_data();
    void displayCaptureDialogs_data();
    void displayCaptureDialogs();
    void cameraPropertiesDialogs_data();
    void cameraPropertiesDialogs();
    void cameraHardwarePreview();
    void addsCameraThroughDialog();
    void sourceFiltersDialogEditsAndRetainsState_data();
    void sourceFiltersDialogEditsAndRetainsState();
    void sourcePanelRequestsRemoval();
    void sourceRemovalDialogHandlesConfirmation_data();
    void sourceRemovalDialogHandlesConfirmation();
    void keepsOutputStatusVisibleWhenResized();
    void loadsSourcePanelWithEmptyState();
    void loadsMixerPanelWithUnavailableChannels();
    void soundSourcesAndDialogs();
    void soundHardwarePreview();
    void outputSettingsDialogDraftsAndTheme();
    void recordingControlsAndGracefulClose();
    void recordingAcceptance();
    void loadsControlsPanelWithVerticalButtons();
    void embedsSourcePanelInFullHeightLeftDock();
    void embedsMixerPanelBelowPreview();
    void hasNoTransitionsDock();
    void embedsControlsPanelAtBottomRight();
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
    m_theme.reset();
    m_settings = std::make_unique<QTemporaryDir>();
    QVERIFY(m_settings->isValid());
    m_theme = std::make_unique<ThemeManager>(m_settings->filePath("settings.ini"));
    QVERIFY(m_theme->isReady());
    // A ready QML root can still have unstable layout bindings.
    QTest::failOnWarning(QRegularExpression(QStringLiteral("Binding loop detected|ReferenceError: uiTheme")));
}

void MainWindowTest::canShowAndClose()
{
    MainWindow window(*m_theme);
    QVERIFY(!window.isVisible());

    window.show();
    QTRY_VERIFY(window.isVisible());

    QVERIFY(window.close());
    QTRY_VERIFY(!window.isVisible());
}

void MainWindowTest::sourcePanelSharesStateAcrossReloadsAndReleasesItWithWindow()
{
    auto window = std::make_unique<MainWindow>(*m_theme);
    window->show();
    QPointer<SourceListModel> state = window->findChild<SourceListModel *>();
    QVERIFY(state);
    QCOMPARE(window->findChildren<SourceListModel *>().size(), 1);
    QCOMPARE(state->parent(), window.get());
    QList<QPointer<QQuickItem>> roots;
    QStringList destructionOrder;
    connect(state, &QObject::destroyed, this, [&] { destructionOrder.append("state"); });

    for (const auto *panelId : {"sources"}) {
        auto *view = window->findChild<QQuickWidget *>(QLatin1String(panelId) + QStringLiteral("QuickWidget"));
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty(QStringLiteral("sourceListModel")).value<QObject *>(), state.data());
        // Exercise the QML context and invokable API, rather than just a C++ pointer.
        QQmlExpression expression(view->rootContext(), view->rootObject(),
            QStringLiteral("sourceListModel.selectSource(sourceListModel.currentSourceId)"));
        QCOMPARE(expression.evaluate().toBool(), true);
        QVERIFY2(!expression.hasError(), qPrintable(expression.error().toString()));
        QPointer<QQuickItem> oldRoot = view->rootObject();
        const QUrl source = view->source();
        view->setSource(QUrl(QStringLiteral("qrc:/missing-state-panel.qml")));
        QTRY_COMPARE(view->status(), QQuickWidget::Error);
        QTRY_VERIFY(oldRoot.isNull());
        view->setSource(source);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty(QStringLiteral("sourceListModel")).value<QObject *>(), state.data());
        QQmlExpression reloaded(view->rootContext(), view->rootObject(),
            QStringLiteral("sourceListModel"));
        QCOMPARE(reloaded.evaluate().value<QObject *>(), state.data());
        QVERIFY2(!reloaded.hasError(), qPrintable(reloaded.error().toString()));
        auto *list = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceList"));
        QVERIFY(list);
        QCOMPARE(list->property("model").value<QObject *>(), state.data());
        roots.append(view->rootObject());
        connect(view->rootObject(), &QObject::destroyed, this,
            [&] { destructionOrder.append("panel"); });
    }

    window.reset();
    QVERIFY(state.isNull());
    for (const auto &root : roots)
        QVERIFY(root.isNull());
    QCOMPARE(destructionOrder, QStringList({"panel", "state"}));
}

void MainWindowTest::sourcePanelFollowsSelectionAcrossReloads()
{
    SourceListModel state(QList<SourceData>{{"a", "First"}, {"b", "Second"}});
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.rootContext()->setContextProperty("sourceListModel", &state);
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    const QUrl source("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml");
    panel.setSource(source);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    const auto row = [&panel](const QString &id) -> QQuickItem * {
        auto *list = panel.rootObject()->findChild<QQuickItem *>("sourceList");
        if (!list) return nullptr;
        auto *content = list->property("contentItem").value<QQuickItem *>();
        if (!content) return nullptr;
        for (auto *item : content->childItems())
            if (item->objectName() == "sourceRow_" + id) return item;
        return nullptr;
    };
    for (const auto &id : {QString("a"), QString("b")}) {
        QTRY_VERIFY(row(id));
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
            row(id)->mapToScene(QPointF(40, row(id)->height() / 2)).toPoint());
        QTRY_COMPARE(state.currentSourceId(), id);
        QTRY_VERIFY(row(id)->property("selected").toBool());
    }
    QVERIFY(!row("a")->property("selected").toBool());
    panel.setSource(QUrl());
    panel.setSource(source);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    QTRY_VERIFY(row("b"));
    QTRY_VERIFY(row("b")->property("selected").toBool());
    QVERIFY(state.selectSource({}));
    QTRY_VERIFY(!row("b")->property("selected").toBool());
}

void MainWindowTest::sourcePanelCanMoveAndToggle_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("recovered") << true;
}

void MainWindowTest::sourcePanelCanMoveAndToggle()
{
    QFETCH(bool, reload);
    SourceListModel state(QList<SourceData>{
        {"a", "Foreground"}, {"b", "Background"}, {"c", "Logo"}});
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(280, 220);
    panel.rootContext()->setContextProperty(QStringLiteral("sourceListModel"), &state);
    const QUrl url(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml"));
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    if (reload) {
        panel.setSource(QUrl(QStringLiteral("qrc:/missing-source-panel.qml")));
        QTRY_COMPARE(panel.status(), QQuickWidget::Error);
        panel.setSource(url);
        QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    }
    const auto find = [&panel](const QString &name) -> QQuickItem * {
        const auto search = [&name](auto &&self, QQuickItem *parent) -> QQuickItem * {
            if (parent->objectName() == name) return parent;
            for (auto *child : parent->childItems()) {
                if (auto *found = self(self, child)) return found;
            }
            return nullptr;
        };
        return search(search, panel.rootObject());
    };
    const auto click = [&panel](QQuickItem *item) {
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    };
    QTRY_VERIFY(find("sourceRow_b"));
    auto *up = find("moveSourceUpButton");
    auto *down = find("moveSourceDownButton");
    QVERIFY(up);
    QVERIFY(down);
    QVERIFY(!up->isEnabled());
    QVERIFY(!down->isEnabled());
    QVERIFY(state.selectSource("a"));
    QTRY_VERIFY(down->isEnabled());
    QVERIFY(!up->isEnabled());
    auto *row = find("sourceRow_b");
    auto *visibility = row->findChild<QQuickItem *>(QStringLiteral("sourceVisibilityButton"));
    auto *lock = row->findChild<QQuickItem *>(QStringLiteral("sourceLockButton"));
    QVERIFY(visibility);
    QVERIFY(lock);
    QVERIFY(visibility->isEnabled());
    QVERIFY(lock->isEnabled());
    QVERIFY(visibility->property("checked").toBool());
    QVERIFY(!lock->property("checked").toBool());
    const auto iconSource = [&panel](QQuickItem *button) {
        QQmlExpression expression(panel.rootContext(), button, QStringLiteral("icon.source"));
        return expression.evaluate().toUrl();
    };
    QCOMPARE(iconSource(visibility), QUrl("qrc:/assets/icons/eye.svg"));
    QCOMPARE(iconSource(lock), QUrl("qrc:/assets/icons/lock-open.svg"));
    click(visibility); // A nonselected row must not steal selection.
    QTRY_VERIFY(!visibility->property("checked").toBool());
    QTRY_COMPARE(iconSource(visibility), QUrl("qrc:/assets/icons/eye-off.svg"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(!state.index(1, 0).data(SourceListModel::VisibleRole).toBool());
    click(lock);
    QTRY_VERIFY(lock->property("checked").toBool());
    QTRY_COMPARE(iconSource(lock), QUrl("qrc:/assets/icons/lock.svg"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(state.index(1, 0).data(SourceListModel::LockedRole).toBool());
    // Model updates must continue to drive presentation after user clicks.
    QVERIFY(state.setSourceVisible("b", true));
    QTRY_VERIFY(visibility->property("checked").toBool());
    QTRY_COMPARE(iconSource(visibility), QUrl("qrc:/assets/icons/eye.svg"));
    click(visibility);
    QTRY_VERIFY(!visibility->property("checked").toBool());
    click(lock);
    QTRY_VERIFY(!lock->property("checked").toBool());
    QTRY_COMPARE(iconSource(lock), QUrl("qrc:/assets/icons/lock-open.svg"));
    click(lock);
    QTRY_VERIFY(lock->property("checked").toBool());
    // Select through the label area, then move a hidden, locked entry.
    QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
        row->mapToScene(QPointF(40, row->height() / 2)).toPoint());
    QTRY_COMPARE(state.currentSourceId(), QString("b"));
    QTRY_VERIFY(up->isEnabled());
    QTRY_VERIFY(down->isEnabled());
    click(up);
    QTRY_COMPARE(state.index(0, 0).data(SourceListModel::IdRole).toString(), QString("b"));
    QTRY_VERIFY(!up->isEnabled());
    QCOMPARE(state.currentSourceId(), QString("b"));
    QTRY_VERIFY(find("sourceRow_b")->y() < find("sourceRow_a")->y());
    click(down);
    click(down);
    QTRY_COMPARE(state.index(2, 0).data(SourceListModel::IdRole).toString(), QString("b"));
    QTRY_VERIFY(!down->isEnabled());
    click(down);
    QCOMPARE(state.currentSourceId(), QString("b"));
    panel.setSource(QUrl());
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    QTRY_VERIFY(find("sourceRow_b"));
    row = find("sourceRow_b");
    QTRY_VERIFY(row->property("selected").toBool());
    visibility = row->findChild<QQuickItem *>(QStringLiteral("sourceVisibilityButton"));
    lock = row->findChild<QQuickItem *>(QStringLiteral("sourceLockButton"));
    QVERIFY(visibility);
    QVERIFY(lock);
    QTRY_VERIFY(!visibility->property("checked").toBool());
    QTRY_VERIFY(lock->property("checked").toBool());
    QTRY_COMPARE(iconSource(visibility), QUrl("qrc:/assets/icons/eye-off.svg"));
    QTRY_COMPARE(iconSource(lock), QUrl("qrc:/assets/icons/lock.svg"));
    QTRY_VERIFY(find("moveSourceUpButton")->isEnabled());
    QTRY_VERIFY(!find("moveSourceDownButton")->isEnabled());
    click(visibility);
    QTRY_VERIFY(visibility->property("checked").toBool());
    click(visibility);
    QTRY_VERIFY(!visibility->property("checked").toBool());
    const QString screenshot = qEnvironmentVariable("OPENLIVE_SOURCE_PANEL_SCREENSHOT");
    if (!screenshot.isEmpty() && !reload)
        QVERIFY(panel.grabFramebuffer().save(screenshot));
    while (state.rowCount() > 0)
        QVERIFY(state.removeSource(
            state.index(0, 0).data(SourceListModel::IdRole).toString()));
    QTRY_VERIFY(find("emptySourcesLabel")->isVisible());
    QVERIFY(!find("moveSourceUpButton")->isEnabled());
    QVERIFY(!find("moveSourceDownButton")->isEnabled());
}

void MainWindowTest::listScrollBarsHandleOverflow_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<QString>("prefix");
    QTest::newRow("sources") << QString("SourcesPanel") << QString("source");
}

void MainWindowTest::listScrollBarsHandleOverflow()
{
    QFETCH(QString, panelName);
    QFETCH(QString, prefix);
    QList<SourceData> sources;
    for (int i = 0; i < 24; ++i)
        sources.append({QString::number(i), QStringLiteral("Source %1").arg(i)});
    SourceListModel state(sources);
    SourceListModel shortState(QList<SourceData>{{"one", "One"}});
    QVERIFY(state.selectSource("0"));
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 150);
    panel.rootContext()->setContextProperty(QStringLiteral("sourceListModel"), &state);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(panelName)));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *list = panel.rootObject()->findChild<QQuickItem *>(prefix + "List");
    auto *bar = panel.rootObject()->findChild<QQuickItem *>(prefix + "ScrollBar");
    QVERIFY(list);
    QVERIFY(bar);
    QTRY_COMPARE(list->property("count").toInt(), 24);
    QTRY_VERIFY(bar->isVisible());
    const QPoint wheelPosition = list->mapToScene(list->boundingRect().center()).toPoint();
    QWheelEvent wheel(wheelPosition, panel.mapToGlobal(wheelPosition), {}, QPoint(0, -120),
        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&panel, &wheel);
    QTRY_VERIFY(list->property("contentY").toReal() > 0);
    QTRY_VERIFY(bar->property("position").toReal() > 0);
    const auto findLast = [&]() -> QQuickItem * {
        auto *content = list->property("contentItem").value<QQuickItem *>();
        if (content) {
            for (auto *item : content->childItems()) {
                if (item->objectName() == prefix + "Row_23")
                    return item;
            }
        }
        return nullptr;
    };
    auto *thumb = bar->property("contentItem").value<QQuickItem *>();
    QVERIFY(thumb);
    QTRY_VERIFY(thumb->height() > 0 && thumb->height() < bar->height());
    const QPoint start = thumb->mapToScene(thumb->boundingRect().center()).toPoint();
    const QPoint end = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - 2)).toPoint();
    QTest::mousePress(&panel, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&panel, end, 20);
    QTest::mouseRelease(&panel, Qt::LeftButton, Qt::NoModifier, end);
    QTRY_VERIFY(list->property("atYEnd").toBool());
    QTRY_VERIFY(findLast());
    const QRectF lastRect = findLast()->mapRectToItem(list, findLast()->boundingRect());
    QVERIFY(lastRect.top() >= -1 && lastRect.bottom() <= list->height() + 1);
    QVERIFY(lastRect.right() <= bar->x());
    QCOMPARE(state.currentSourceId(), QString("0"));
    panel.resize(238, 1000);
    QTRY_VERIFY(!bar->isVisible());
    panel.resize(238, 150);
    QTRY_VERIFY(bar->isVisible());
    // A short list needs no scrollbar even in the smaller panel.
    panel.rootContext()->setContextProperty(QStringLiteral("sourceListModel"), &shortState);
    QTRY_COMPARE(list->property("count").toInt(), 1);
    QTRY_VERIFY(!bar->isVisible());
}

void MainWindowTest::hasWindowShell()
{
    MainWindow window(*m_theme);
    window.show();

    QVERIFY(!window.findChild<QDockWidget *>("scenesDock"));
    QVERIFY(!window.findChild<QQuickWidget *>("scenesQuickWidget"));
    QVERIFY(!window.findChild<QAction *>("scenesDockViewAction"));
    QCOMPARE(window.findChildren<QDockWidget *>().size(), 3);
    QVERIFY(window.metaObject()->indexOfSlot("showSceneNameDialog(QString,QString)") < 0);
    QVERIFY(window.metaObject()->indexOfSlot("showSceneRemovalDialog(QString)") < 0);
    QVERIFY(window.centralWidget());
    QVERIFY(window.centralWidget()->isVisible());
    QVERIFY(window.menuBar()->isVisible());
    QCOMPARE(window.menuBar()->actions().size(), 3);
    QVERIFY(window.findChildren<QToolBar *>().isEmpty());
    QVERIFY(window.statusBar()->isVisible());
    auto *pages = window.findChild<QStackedWidget *>(QStringLiteral("previewPages"));
    auto *error = window.findChild<QLabel *>(QStringLiteral("previewErrorLabel"));
    QVERIFY(pages && error);
    if (QGuiApplication::platformName() == "offscreen" || QGuiApplication::platformName() == "minimal") {
        QVERIFY(!window.findChild<PreviewWidget *>());
        QCOMPARE(pages->currentWidget(), error);
        QVERIFY(error->isVisible());
        QVERIFY(error->text().contains(QStringLiteral("不支持 OpenGL")));
    } else {
        auto *preview = window.findChild<PreviewWidget *>(QStringLiteral("previewWidget"));
        QVERIFY(preview);
        QCOMPARE(pages->currentWidget(), preview);
        QVERIFY(preview->isVisible());
    }
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("previewStatus")));
}

void MainWindowTest::previewDisplaysImageSources()
{
    MainWindow window(*m_theme);
    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required for preview integration testing.");
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(preview->isValid());
    QVERIFY2(preview->lastError().isEmpty(), qPrintable(preview->lastError()));
    auto *pages = window.findChild<QStackedWidget *>("previewPages");
    QCOMPARE(pages->currentWidget(), preview);
    auto *sources = window.findChild<SourceListModel *>();
    QVERIFY(sources);
    QCOMPARE(sources->rowCount(), 0);
    const auto empty = grabPreview(*preview);
    QCOMPARE(empty.pixelColor(empty.width() / 2, empty.height() / 2), QColor(Qt::black));
    QTemporaryDir images;
    QImage image(1920, 1080, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.fillRect(0, 0, 960, 540, Qt::red);
        painter.fillRect(960, 0, 960, 540, Qt::green);
        painter.fillRect(0, 540, 960, 540, Qt::blue);
        painter.fillRect(960, 540, 960, 540, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(800, 450, 320, 180, Qt::transparent);
    }
    const QString imagePath = images.filePath("corners.png");
    QVERIFY(image.save(imagePath));
    auto *controller = window.findChild<ImageSourceController *>();
    QVERIFY(controller);
    QObject request;
    controller->addImage("Corners", imagePath, &request);
    QTRY_COMPARE(sources->rowCount(), 1);
    for (const auto *name : {"sourcesQuickWidget", "mixerQuickWidget", "controlsQuickWidget"}) {
        auto *panel = window.findChild<QQuickWidget *>(QLatin1String(name));
        QVERIFY(panel);
        QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
        QVERIFY(panel->rootObject());
        QCOMPARE(panel->quickWindow()->rendererInterface()->graphicsApi(), QSGRendererInterface::OpenGL);
    }

    const auto verifyFrame = [&] {
        const QImage frame = grabPreview(*preview);
        QVERIFY(!frame.isNull());
        const double scale = qMin(frame.width() / 1920.0, frame.height() / 1080.0);
        const double width = 1920 * scale;
        const double height = 1080 * scale;
        const double x = (frame.width() - width) / 2;
        const double y = (frame.height() - height) / 2;
        const auto sample = [&](double u, double v) {
            return frame.pixelColor(int(x + u * width), int(y + v * height));
        };
        QCOMPARE(sample(.1, .1), QColor(Qt::red));
        QCOMPARE(sample(.9, .1), QColor(Qt::green));
        QCOMPARE(sample(.1, .9), QColor(Qt::blue));
        QCOMPARE(sample(.9, .9), QColor(Qt::white));
        QCOMPARE(sample(.5, .5), QColor(Qt::black));
        QVERIFY(x > 2 || y > 2);
        const QColor surrounding = y > 2 ? frame.pixelColor(frame.width() / 2, int(y / 2))
                                         : frame.pixelColor(int(x / 2), frame.height() / 2);
        QCOMPARE(surrounding, m_theme->color("preview.surrounding"));
    };
    verifyFrame();
    QVERIFY(m_theme->setTheme("light"));
    verifyFrame();
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/mainwindow-preview.png"));
    window.resize(qMax(window.minimumWidth(), window.width() - 40), window.height());
    QCoreApplication::processEvents();
    verifyFrame();
    QVERIFY(m_theme->setTheme("dark"));
    verifyFrame();
    QImage overlay(600, 400, QImage::Format_RGBA8888);
    overlay.fill(Qt::yellow);
    const QString overlayPath = images.filePath("overlay.png");
    QVERIFY(overlay.save(overlayPath));
    controller->addImage("Overlay", overlayPath, &request);
    QTRY_COMPARE(sources->rowCount(), 2);
    const QString overlayId = sources->currentSourceId();
    const auto sampleCanvas = [&](double u, double v) {
        const QImage frame = grabPreview(*preview);
        const double scale = qMin(frame.width() / 1920.0, frame.height() / 1080.0);
        return frame.pixelColor(int((frame.width() - 1920 * scale) / 2 + u * 1920 * scale),
            int((frame.height() - 1080 * scale) / 2 + v * 1080 * scale));
    };
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    const auto find = [&](const QString &objectName) {
        const auto search = [&](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == objectName) return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child)) return found;
            return nullptr;
        };
        return search(search, panel->rootObject());
    };
    QTRY_VERIFY(find("sourceRow_" + overlayId));
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/mainwindow-image-sources.png"));
    auto *row = find("sourceRow_" + overlayId);
    auto *visibility = row->findChild<QQuickItem *>("sourceVisibilityButton");
    auto *lock = row->findChild<QQuickItem *>("sourceLockButton");
    auto *down = find("moveSourceDownButton");
    QVERIFY(visibility && lock && down);
    const auto click = [&](QQuickItem *item) {
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(item->boundingRect().center()).toPoint());
    };
    click(lock);
    click(visibility);
    QTRY_VERIFY(!sources->index(0, 0).data(SourceListModel::VisibleRole).toBool());
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    click(visibility);
    QTRY_VERIFY(sources->index(0, 0).data(SourceListModel::VisibleRole).toBool());
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    click(down);
    QTRY_COMPARE(sources->index(1, 0).data(SourceListModel::IdRole).toString(), overlayId);
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    QVERIFY(sources->moveSource(overlayId, -1));
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    QVERIFY(sources->removeSource(overlayId));
    verifyFrame();
    QVERIFY(sources->removeSource(sources->index(0, 0).data(SourceListModel::IdRole).toString()));
    QCOMPARE(sampleCanvas(.6, .6), QColor(Qt::black));
    QVERIFY(window.close());
}

void MainWindowTest::previewEditsModelAndSynchronizesSelection()
{
    MainWindow window(*m_theme);
    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview) QSKIP("Native desktop OpenGL is required for mouse editing integration.");
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    auto *model = window.findChild<SourceListModel *>();
    auto *controller = window.findChild<ImageSourceController *>();
    QTemporaryDir dir;
    QImage background(1200, 800, QImage::Format_RGBA8888);
    background.fill(Qt::red);
    QImage foreground(400, 200, QImage::Format_RGBA8888);
    foreground.fill(Qt::blue);
    const QString backPath = dir.filePath("back.png"), frontPath = dir.filePath("front.png");
    QVERIFY(background.save(backPath));
    QVERIFY(foreground.save(frontPath));
    QObject request;
    controller->addImage("Background", backPath, &request);
    QTRY_COMPARE(model->rowCount(), 1);
    const QString backId = model->currentSourceId();
    controller->addImage("Foreground", frontPath, &request);
    QTRY_COMPARE(model->rowCount(), 2);
    const QString frontId = model->currentSourceId();
    const auto currentTransform = [&] {
        return model->index(0, 0).data(SourceListModel::TransformRole).value<SourceTransform>();
    };
    const auto canvasPoint = [&](const QPointF &point) {
        return SourceGeometry::canvasToWidget(preview->size()).map(point).toPoint();
    };
    grabPreview(*preview);
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(600, 500)));
    QCOMPARE(model->currentSourceId(), backId);
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(960, 540)));
    QCOMPARE(model->currentSourceId(), frontId);
    const auto move = [&](const QPoint &from, const QPoint &to) {
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(preview, to);
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, to);
    };
    const SourceTransform before = currentTransform();
    const QPoint from = canvasPoint(before.center), to = from + QPoint(30, 15);
    move(from, to);
    const auto inverse = SourceGeometry::canvasToWidget(preview->size()).inverted();
    const auto expectedCenter = before.center + inverse.map(QPointF(to)) - inverse.map(QPointF(from));
    QVERIFY(std::hypot(currentTransform().center.x() - expectedCenter.x(),
                       currentTransform().center.y() - expectedCenter.y()) < 1e-6);
    const auto moved = currentTransform();
    window.resize(qMax(window.minimumWidth(), window.width() - 40), window.height());
    QCoreApplication::processEvents();
    QVERIFY(currentTransform() == moved);
    move(canvasPoint(moved.center), canvasPoint(moved.center) + QPoint(10, 5));
    QVERIFY(currentTransform().center != moved.center);
    SourceTransform rotated = currentTransform();
    rotated.rotation = 30;
    rotated.size = QSizeF(450, 250);
    QVERIFY(model->setSourceTransform(frontId, rotated));
    const auto frame = grabPreview(*preview);
    const auto center = canvasPoint(rotated.center);
    QCOMPARE(frame.pixelColor(qRound(center.x() * preview->devicePixelRatioF()),
                              qRound(center.y() * preview->devicePixelRatioF())), QColor(Qt::blue));
    QVERIFY(model->setSourceLocked(frontId, true));
    QVERIFY(!model->setSourceTransform(frontId, before));
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(model->currentSourceId(), backId);
    QVERIFY(model->selectSource(frontId));
    const auto locked = grabPreview(*preview);
    QVERIFY(model->setSourceLocked(frontId, false));
    const auto unlocked = grabPreview(*preview);
    QVERIFY(locked != unlocked);
    QVERIFY(model->setSourceVisible(frontId, false));
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(model->currentSourceId(), backId);
    QVERIFY(model->setSourceVisible(frontId, true));
    QVERIFY(model->selectSource(frontId));
    QVERIFY(m_theme->setTheme("light"));
    QCoreApplication::processEvents();
    const auto overlayFrame = grabPreview(*preview);
    const auto corner = canvasPoint(SourceGeometry::sourceToCanvas(rotated).map(QPointF()));
    QCOMPARE(overlayFrame.pixelColor(qRound(corner.x() * preview->devicePixelRatioF()),
                                    qRound(corner.y() * preview->devicePixelRatioF())),
             m_theme->color("border.focus"));
    QVERIFY(overlayFrame.save(QCoreApplication::applicationDirPath() + "/preview-transforms.png"));
    QVERIFY(m_theme->setTheme("dark"));
    QCOMPARE(window.findChild<QStackedWidget *>("previewPages")->currentWidget(), preview);
    QVERIFY(preview->lastError().isEmpty());
    QVERIFY(window.close());
}

void MainWindowTest::previewShowsRenderingError()
{
    MainWindow window(*m_theme);
    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required to inject a preview rendering error.");
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *pages = window.findChild<QStackedWidget *>("previewPages");
    auto *error = window.findChild<QLabel *>("previewErrorLabel");
    QVERIFY(pages && error);
    auto *model = window.findChild<SourceListModel *>();
    QVERIFY(model->addSource({"example", "Logo"}));
    preview->sourceError("example", "Texture upload failed");
    QTRY_VERIFY(window.statusBar()->currentMessage().contains("Logo"));
    QVERIFY(window.statusBar()->currentMessage().contains("Texture upload failed"));
    QCOMPARE(pages->currentWidget(), preview);
    QVERIFY(preview->lastError().isEmpty());
    const QString details = QStringLiteral("<b>测试渲染错误</b>");
    preview->renderingError(details);
    QCOMPARE(pages->currentWidget(), preview);
    QTRY_COMPARE(pages->currentWidget(), error);
    QVERIFY(error->isVisible());
    QVERIFY(!preview->isVisible());
    QCOMPARE(error->textFormat(), Qt::PlainText);
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + details);
    QVERIFY(m_theme->setTheme("light"));
    QTRY_COMPARE(error->palette().color(QPalette::WindowText), m_theme->color("status.error"));
    QTRY_COMPARE(error->palette().color(QPalette::Window), m_theme->color("background.panel"));
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + details);
    QCOMPARE(pages->currentWidget(), error);
    QVERIFY(window.close());
}

void MainWindowTest::dockTitlesStayVerticallyCentered()
{
    // Observe the alignment passed to Qt's text renderer, without depending on
    // font glyph pixels or title-bar dimensions at a particular DPI.
    class TitleStyle final : public QProxyStyle {
    public:
        explicit TitleStyle(QStyle *base) : QProxyStyle(base) {}
        QStringList titles;
        mutable QMap<QString, int> alignments;
        void drawItemText(QPainter *painter, const QRect &rect, int flags,
                          const QPalette &palette, bool enabled, const QString &text,
                          QPalette::ColorRole role = QPalette::NoRole) const override
        {
            if (titles.contains(text)) alignments.insert(text, flags);
            QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, text, role);
        }
    };
    auto *base = QStyleFactory::create(QApplication::style()->objectName());
    QVERIFY(base);
    TitleStyle style(base);
    MainWindow window(*m_theme);
    window.show();
    const auto docks = window.findChildren<QDockWidget *>();
    QCOMPARE(docks.size(), 3);
    for (auto *dock : docks) {
        QVERIFY(!dock->titleBarWidget());
        style.titles.append(dock->windowTitle());
        dock->setStyle(&style);
    }
    for (const auto *theme : {"dark", "light"}) {
        QVERIFY(m_theme->setTheme(theme));
        style.alignments.clear();
        for (auto *dock : docks) {
            QVERIFY(!dock->grab().isNull());
            QVERIFY(style.alignments.contains(dock->windowTitle()));
            const int flags = style.alignments.value(dock->windowTitle());
            QCOMPARE(flags & int(Qt::AlignVertical_Mask), int(Qt::AlignVCenter));
            QCOMPARE(flags & int(Qt::AlignHorizontal_Mask), int(Qt::AlignLeft));
            QVERIFY(!dock->titleBarWidget());
        }
    }
}

void MainWindowTest::themeMenuSwitchesAndRestoresPreference()
{
    QVERIFY(m_theme->setTheme("light"));
    ThemeManager restored(m_theme->settingsFile());
    MainWindow window(restored);
    window.show();
    auto *menu = window.findChild<QMenu *>("themeMenu");
    auto *dark = window.findChild<QAction *>("darkThemeAction");
    auto *light = window.findChild<QAction *>("lightThemeAction");
    QVERIFY(menu && dark && light);
    QCOMPARE(window.menuBar()->actions()[1]->menu(), menu);
    QCOMPARE(window.menuBar()->actions()[0]->menu(), window.findChild<QMenu *>("viewMenu"));
    QCOMPARE(menu->actions(), QList<QAction *>({dark, light}));
    QVERIFY(dark->isCheckable() && light->isCheckable());
    QVERIFY(dark->actionGroup() && dark->actionGroup() == light->actionGroup());
    QVERIFY(dark->actionGroup()->isExclusive());
    QVERIFY(light->isChecked() && !dark->isChecked());
    auto *viewMenu = window.findChild<QMenu *>("viewMenu");
    QVERIFY(!viewMenu->actions().contains(dark) && !viewMenu->actions().contains(light));
    const auto panels = window.findChildren<QQuickWidget *>();
    QList<QPointer<QQuickItem>> roots;
    for (auto *panel : panels) {
        QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
        roots.append(panel->rootObject());
    }
    QSignalSpy changed(&restored, &ThemeManager::themeChanged);
    for (auto *action : {dark, dark, light}) {
        action->trigger();
        QCOMPARE(restored.themeId(), action->data().toString());
        QVERIFY(action->isChecked());
        QVERIFY(dark->isChecked() != light->isChecked());
        ThemeManager saved(restored.settingsFile());
        QCOMPARE(saved.themeId(), restored.themeId());
        for (int i = 0; i < panels.size(); ++i) {
            QCOMPARE(panels[i]->rootObject(), roots[i].data());
            QTRY_COMPARE(roots[i]->property("color").value<QColor>(), restored.color("background.panel"));
        }
    }
    QCOMPARE(changed.size(), 2); // Re-selecting the active theme keeps it selected.
    QVERIFY(restored.setTheme("dark"));
    QVERIFY(dark->isChecked() && !light->isChecked());
    ThemeManager restarted(restored.settingsFile());
    MainWindow reopened(restarted);
    QVERIFY(reopened.findChild<QAction *>("darkThemeAction")->isChecked());
    QVERIFY(!reopened.findChild<QAction *>("lightThemeAction")->isChecked());
}

void MainWindowTest::themeMenuRecoversFromSaveFailure()
{
    const auto blocker = m_settings->filePath("blocker");
    {
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("keep"), qint64(4));
    }
    ThemeManager theme(blocker + "/settings.ini");
    MainWindow window(theme);
    window.show();
    auto *dark = window.findChild<QAction *>("darkThemeAction");
    auto *light = window.findChild<QAction *>("lightThemeAction");
    QVERIFY(dark && light);
    const auto colors = theme.colors();
    const auto palette = window.palette();
    QSignalSpy changed(&theme, &ThemeManager::themeChanged);
    light->trigger();
    QVERIFY(!theme.lastError().isEmpty());
    QVERIFY(window.statusBar()->currentMessage().contains(theme.lastError()));
    QCOMPARE(theme.themeId(), "dark");
    QCOMPARE(theme.colors(), colors);
    QCOMPARE(window.palette(), palette);
    QVERIFY(dark->isChecked() && !light->isChecked());
    QCOMPARE(changed.size(), 0);
    QVERIFY(QFile::remove(blocker));
    light->trigger();
    QCOMPARE(theme.themeId(), "light");
    QVERIFY(light->isChecked() && !dark->isChecked());
    QVERIFY(theme.lastError().isEmpty());
    QVERIFY(window.statusBar()->currentMessage().isEmpty());
    QCOMPARE(changed.size(), 1);
    ThemeManager restored(theme.settingsFile());
    QCOMPARE(restored.themeId(), "light");
}

void MainWindowTest::themeRestoresAndUpdatesOuterWidgets()
{
    const QPalette applicationPalette = QApplication::palette();
    QVERIFY(m_theme->setTheme("light"));
    m_theme = std::make_unique<ThemeManager>(m_settings->filePath("settings.ini"));
    QCOMPARE(m_theme->themeId(), "light");
    MainWindow window(*m_theme);
    window.show();
    QCOMPARE(window.palette().color(QPalette::Window), m_theme->color("background.window"));
    QCOMPARE(window.palette().color(QPalette::Text), m_theme->color("text.primary"));
    const auto views = window.findChildren<QQuickWidget *>();
    QCOMPARE(views.size(), 3);
    QList<QPointer<QQuickItem>> roots;
    for (auto *view : views) {
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty("uiTheme").value<QObject *>(), m_theme.get());
        QCOMPARE(view->quickWindow()->color(), m_theme->color("background.panel"));
        roots.append(view->rootObject());
    }
    // Exercise an actual QML binding to the nested QColor values, not just the
    // context pointer. The existing panel internals migrate in later steps.
    QQmlComponent probe(views.first()->engine());
    probe.setData("import QtQuick\nQtObject { property color themedColor: uiTheme.colors.text.primary }", QUrl());
    QTRY_COMPARE(probe.status(), QQmlComponent::Ready);
    std::unique_ptr<QObject> binding(probe.create(views.first()->rootContext()));
    QVERIFY(binding);
    QCOMPARE(binding->property("themedColor").value<QColor>(), m_theme->color("text.primary"));
    auto *dock = window.findChild<QDockWidget *>("sourcesDock");
    QVERIFY(dock);
    dock->setFloating(true);
    QTRY_VERIFY(dock->isFloating());
    QVERIFY(m_theme->setTheme("dark"));
    QTRY_COMPARE(binding->property("themedColor").value<QColor>(), m_theme->color("text.primary"));
    QCOMPARE(window.palette().color(QPalette::Window), m_theme->color("background.window"));
    QCOMPARE(dock->palette().color(QPalette::Window), m_theme->color("background.window"));
    QCOMPARE(window.palette().color(QPalette::Disabled, QPalette::Text), m_theme->color("text.disabled"));
    auto *metrics = window.findChild<QLabel *>("outputMetrics");
    QVERIFY(metrics);
    QTRY_COMPARE(metrics->palette().color(QPalette::WindowText), m_theme->color("text.primary"));
    for (int i = 0; i < views.size(); ++i) {
        QCOMPARE(views[i]->rootObject(), roots[i].data());
        QCOMPARE(views[i]->quickWindow()->color(), m_theme->color("background.panel"));
    }
    auto *error = window.findChild<QLabel *>("previewErrorLabel");
    QVERIFY(error);
    QTRY_COMPARE(error->palette().color(QPalette::WindowText), m_theme->color("status.error"));
    QTRY_COMPARE(error->palette().color(QPalette::Window), m_theme->color("background.panel"));
    QTRY_COMPARE(window.centralWidget()->palette().color(QPalette::Window), m_theme->color("preview.surrounding"));
    dock->setFloating(false);
    const auto titleButtons = dock->findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(!titleButtons.isEmpty());
    for (auto *button : titleButtons) {
        for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
            const auto icon = button->icon().pixmap(16, 16, mode).toImage();
            QVERIFY(!icon.isNull());
            QColor strongest;
            for (int y = 0; y < icon.height(); ++y)
                for (int x = 0; x < icon.width(); ++x)
                    if (!strongest.isValid() || icon.pixelColor(x, y).alpha() > strongest.alpha())
                        strongest = icon.pixelColor(x, y);
            QVERIFY(strongest.alpha() > 0);
            strongest.setAlpha(255);
            QCOMPARE(strongest.rgba(), m_theme->color(mode == QIcon::Disabled ? "icon.disabled" : "icon.primary").rgba());
        }
    }
    QCOMPARE(QApplication::palette(), applicationPalette);
}

void MainWindowTest::sourceThemes_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("themeId");
    for (const auto &theme : {QString("dark"), QString("light")}) {
        QTest::newRow(qPrintable("sources-" + theme)) << QString("SourcesPanel") << QString("source") << theme;
    }
}

void MainWindowTest::sourceThemes()
{
    QFETCH(QString, panelName);
    QFETCH(QString, prefix);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    QList<SourceData> sources;
    for (int i = 0; i < 24; ++i)
        sources.append({QString::number(i), QString("Source %1").arg(i),
                        ImageConfig{}, {}, i != 0, i == 0});
    SourceListModel state(sources);
    QVERIFY(state.selectSource("0"));
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.rootContext()->setContextProperty("sourceListModel", &state);
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(280, 220);
    const QUrl url(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(panelName));
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    const auto find = [&panel](const QString &name) {
        const auto visit = [&name](auto &&self, QQuickItem *item) -> QQuickItem * {
            if (item->objectName() == name) return item;
            for (auto *child : item->childItems())
                if (auto *found = self(self, child)) return found;
            return nullptr;
        };
        return visit(visit, panel.rootObject());
    };
    // QWidget QTest::mouseMove uses the OS cursor when no button is pressed,
    // which does not deliver hover events to the offscreen platform.
    const auto moveMouse = [&panel](const QPoint &position) {
        QMouseEvent event(QEvent::MouseMove, position, panel.mapToGlobal(position),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&panel, &event);
    };
    QTRY_VERIFY(find(prefix + "Row_0"));
    QPointer<QQuickItem> originalRoot = panel.rootObject();
    for (const auto &id : {themeId, themeId == "dark" ? QString("light") : QString("dark")}) {
        QVERIFY(m_theme->setTheme(id));
        QCOMPARE(panel.rootObject(), originalRoot.data());
        QTRY_COMPARE(panel.rootObject()->property("color").value<QColor>(), m_theme->color("background.panel"));
        auto *row = find(prefix + "Row_0");
        QVERIFY(row);
        QCOMPARE(row->property("color").value<QColor>(), m_theme->color("background.selected"));
        const auto labelText = QString("Source 0");
        bool foundLabel = false;
        for (auto *label : row->findChildren<QQuickItem *>()) {
            if (label->property("text").toString() == labelText) {
                QCOMPARE(label->property("color").value<QColor>(), m_theme->color("text.primary"));
                foundLabel = true;
            }
        }
        QVERIFY(foundLabel);
        auto *row1 = find(prefix + "Row_1");
        QVERIFY(row1);
        const auto rowCenter = [](QQuickItem *item) {
            return item->mapToScene(item->boundingRect().center()).toPoint();
        };
        moveMouse(rowCenter(row1));
        QTRY_COMPARE(row1->property("color").value<QColor>(), m_theme->color("background.hovered"));
        moveMouse(rowCenter(row));
        QTRY_COMPARE(row->property("color").value<QColor>(), m_theme->color("background.selected"));
        moveMouse(QPoint(panel.width() - 2, panel.height() - 2));
        QTRY_COMPARE(row1->property("color").value<QColor>(), QColor(Qt::transparent));
        auto *add = find("addSourceButton");
        QVERIFY(add && add->isEnabled());
        QCOMPARE(QQmlProperty::read(add, "icon.color").value<QColor>(), m_theme->color("icon.primary"));
        auto *background = add->property("background").value<QQuickItem *>();
        QVERIFY(background);
        add->forceActiveFocus(Qt::TabFocusReason);
        QTRY_VERIFY(add->hasActiveFocus());
        QCOMPARE(QQmlProperty::read(background, "border.color").value<QColor>(), m_theme->color("border.focus"));
        const QPoint position = add->mapToScene(add->boundingRect().center()).toPoint();
        moveMouse(position);
        QTRY_VERIFY(add->property("hovered").toBool());
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.hovered"));
        QTest::mousePress(&panel, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.pressed"));
        QTest::mouseRelease(&panel, Qt::LeftButton, Qt::NoModifier, position);
        auto *bar = find(prefix + "ScrollBar");
        QVERIFY(bar && bar->isVisible());
        auto *thumb = bar->property("contentItem").value<QQuickItem *>();
        QVERIFY(thumb);
        moveMouse(QPoint(2, 2));
        QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.normal"));
        const QPoint thumbPosition = thumb->mapToScene(thumb->boundingRect().center()).toPoint();
        moveMouse(thumbPosition);
        QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.hovered"));
        QTest::mousePress(&panel, Qt::LeftButton, Qt::NoModifier, thumbPosition);
        QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.pressed"));
        QTest::mouseRelease(&panel, Qt::LeftButton, Qt::NoModifier, thumbPosition);
        if (prefix == "source") {
            auto *filters = find("sourceFiltersButton");
            QVERIFY(filters && filters->isEnabled());
            QCOMPARE(QQmlProperty::read(filters, "icon.color").value<QColor>(), m_theme->color("icon.primary"));
            QVERIFY(find("sourceVisibilityButton")->property("checked").toBool() == false);
            QVERIFY(find("sourceLockButton")->property("checked").toBool());
        }
        QCOMPARE(state.currentSourceId(), "0");
    }
    panel.setSource(QUrl());
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    QTRY_VERIFY(find(prefix + "Row_0"));
    QCOMPARE(find(prefix + "Row_0")->property("color").value<QColor>(), m_theme->color("background.selected"));
    QCOMPARE(state.currentSourceId(), "0");
    if (prefix == "source") {
        while (state.rowCount() > 0)
            QVERIFY(state.removeSource(
                state.index(0, 0).data(SourceListModel::IdRole).toString()));
        auto *label = find("emptySourcesLabel");
        QVERIFY(label);
        QTRY_VERIFY(label->isVisible());
        QCOMPARE(label->property("color").value<QColor>(), m_theme->color("text.secondary"));
    }
}

void MainWindowTest::mixerAndControlThemes_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("themeId");
    for (const auto &theme : {QString("dark"), QString("light")}) {
        QTest::newRow(qPrintable("mixer-" + theme)) << QString("MixerPanel") << QString("mixer") << theme;
        QTest::newRow(qPrintable("controls-" + theme)) << QString("ControlsPanel") << QString("controls") << theme;
    }
}

void MainWindowTest::mixerAndControlThemes()
{
    QFETCH(QString, panelName);
    QFETCH(QString, prefix);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    if (prefix == "mixer") {
        AudioSourceModel audio;
        QQuickWidget panel;
        panel.rootContext()->setContextProperty("uiTheme",m_theme.get());
        panel.rootContext()->setContextProperty("audioSourceModel",&audio);
        panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
        panel.resize(286,240);
        panel.setSource(QUrl("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml"));
        QTRY_COMPARE(panel.status(),QQuickWidget::Ready); panel.show();
        auto *root = panel.rootObject(); QVERIFY(root);
        auto *add = findQuickItem(root,"addSoundSourceButton"); QVERIFY(add && add->isEnabled());
        const auto id = audio.addSource("Mic", "microphone", "missing-theme-audio"); QVERIFY(!id.isEmpty());
        QTRY_VERIFY(findQuickItem(root,id + "MuteButton"));
        for (const auto &theme : {themeId,themeId == "dark" ? QString("light") : QString("dark")}) {
            QVERIFY(m_theme->setTheme(theme));
            QCOMPARE(panel.rootObject(),root);
            QTRY_COMPARE(root->property("color").value<QColor>(),m_theme->color("background.panel"));
            auto *mute = findQuickItem(root,id + "MuteButton"); QVERIFY(mute);
            QCOMPARE(QQmlProperty::read(mute,"icon.color").value<QColor>(),m_theme->color("icon.primary"));
            auto *slider = findQuickItem(root,id + "VolumeSlider"); QVERIFY(slider && slider->isEnabled());
            auto *handle = slider->property("handle").value<QQuickItem *>(); QVERIFY(handle);
            QCOMPARE(handle->property("color").value<QColor>(),m_theme->color("slider.handle"));
            if (QGuiApplication::platformName() != "offscreen")
                QVERIFY(panel.grabFramebuffer().save(QCoreApplication::applicationDirPath() + "/sound-sources-" + theme + ".png"));
        }
        return;
    }
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(286, 220);
    const QUrl url(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(panelName));
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    QPointer<QQuickItem> root = panel.rootObject();
    QList<QQuickItem *> buttons, sliders, tracks;
    const auto inspect = [&](auto &&self, QQuickItem *item) -> void {
        if (item->objectName().endsWith("Button")) buttons.append(item);
        if (item->objectName().endsWith("VolumeSlider")) sliders.append(item);
        if (item->objectName() == "levelTrack") tracks.append(item);
        for (auto *child : item->childItems()) self(self, child);
    };
    inspect(inspect, root);
    QCOMPARE(buttons.size(), prefix == "mixer" ? 4 : 3);
    QCOMPARE(sliders.size(), prefix == "mixer" ? 2 : 0);
    QCOMPARE(tracks.size(), prefix == "mixer" ? 4 : 0);
    const auto moveMouse = [&panel](const QPoint &position) {
        QMouseEvent event(QEvent::MouseMove, position, panel.mapToGlobal(position),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&panel, &event);
    };
    for (const auto &id : {themeId, themeId == "dark" ? QString("light") : QString("dark")}) {
        QVERIFY(m_theme->setTheme(id));
        QCOMPARE(panel.rootObject(), root.data());
        QTRY_COMPARE(root->property("color").value<QColor>(), m_theme->color("background.panel"));
        for (auto *button : buttons) {
            QVERIFY(!button->isEnabled());
            QCOMPARE(QQmlProperty::read(button, "icon.color").value<QColor>(), m_theme->color("icon.disabled"));
            if (prefix == "controls") {
                auto *background = button->property("background").value<QQuickItem *>();
                QVERIFY(background);
                QCOMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.disabled"));
                QCOMPARE(QQmlProperty::read(button, "palette.buttonText").value<QColor>(), m_theme->color("text.disabled"));
            }
        }
        for (auto *track : tracks) {
            QCOMPARE(track->property("color").value<QColor>(), m_theme->color("background.input"));
            QCOMPARE(QQmlProperty::read(track, "border.color").value<QColor>(), m_theme->color("border.subtle"));
            QVERIFY(track->childItems().isEmpty()); // No simulated level fill.
        }
        for (auto *slider : sliders) {
            QVERIFY(!slider->isEnabled());
            QCOMPARE(slider->property("value").toReal(), .5);
            auto *handle = slider->property("handle").value<QQuickItem *>();
            auto *background = slider->property("background").value<QQuickItem *>();
            QVERIFY(handle && background);
            QCOMPARE(background->property("color").value<QColor>(), m_theme->color("slider.track"));
            QCOMPARE(handle->property("color").value<QColor>(), m_theme->color("slider.handleDisabled"));
            // Enable presentation only; there is still no audio gain connection.
            slider->setEnabled(true);
            slider->forceActiveFocus(Qt::TabFocusReason);
            QTRY_COMPARE(handle->property("color").value<QColor>(), m_theme->color("slider.handle"));
            QCOMPARE(QQmlProperty::read(handle, "border.color").value<QColor>(), m_theme->color("border.focus"));
            slider->setEnabled(false);
        }
        for (auto *label : root->findChildren<QQuickItem *>()) {
            if (label->objectName() == "channelStatus" || label->objectName() == "channelValue")
                QCOMPARE(label->property("color").value<QColor>(), m_theme->color("text.secondary"));
        }
        // Check enabled presentation without enabling any production feature.
        auto *button = buttons.first();
        auto *background = button->property("background").value<QQuickItem *>();
        QVERIFY(background);
        button->setEnabled(true);
        moveMouse(QPoint(1, 1));
        QCOMPARE(QQmlProperty::read(button, "icon.color").value<QColor>(), m_theme->color("icon.primary"));
        button->forceActiveFocus(Qt::TabFocusReason);
        QTRY_COMPARE(QQmlProperty::read(background, "border.color").value<QColor>(), m_theme->color("border.focus"));
        const QPoint position = button->mapToScene(button->boundingRect().center()).toPoint();
        moveMouse(position);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.hovered"));
        QTest::mousePress(&panel, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.pressed"));
        QTest::mouseRelease(&panel, Qt::LeftButton, Qt::NoModifier, position);
        button->setEnabled(false);
        panel.resize(140, 100);
        QTRY_COMPARE(root->size(), QSizeF(panel.size()));
        for (const auto &axis : {QString("Horizontal"), QString("Vertical")}) {
            auto *bar = root->findChild<QQuickItem *>(prefix + axis + "ScrollBar");
            QVERIFY(bar);
            QTRY_VERIFY(bar->property("size").toReal() < 1);
            auto *thumb = bar->property("contentItem").value<QQuickItem *>();
            QVERIFY(thumb);
            moveMouse(QPoint(1, 1));
            QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.normal"));
            const auto point = thumb->mapToScene(thumb->boundingRect().center()).toPoint();
            moveMouse(point);
            QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.hovered"));
            QTest::mousePress(&panel, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE(thumb->property("color").value<QColor>(), m_theme->color("scrollbar.thumb.pressed"));
            QTest::mouseRelease(&panel, Qt::LeftButton, Qt::NoModifier, point);
        }
        panel.resize(286, 220);
        QTRY_COMPARE(root->size(), QSizeF(panel.size()));
    }
    panel.setSource(QUrl());
    QVERIFY(root.isNull());
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    QCOMPARE(panel.rootObject()->property("color").value<QColor>(), m_theme->color("background.panel"));
}

void MainWindowTest::basicDialogThemes_data()
{
    QTest::addColumn<QString>("dialogName");
    QTest::addColumn<QString>("themeId");
    for (const auto &theme : {QString("dark"), QString("light")})
        for (const auto &name : {QString("AboutDialog"), QString("SourceAddDialog"), QString("SourceRemovalDialog")})
            QTest::newRow(qPrintable(name + "-" + theme)) << name << theme;
}

void MainWindowTest::basicDialogThemes()
{
    QFETCH(QString, dialogName);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<SourceListModel *>();
    QVERIFY(state);
    QVariantMap properties;
    if (dialogName == "AboutDialog") {
        properties = {{"productName", "OpenLive Studio"}, {"versionText", "Development"}, {"qtVersion", qVersion()}};
    } else {
        properties = {{"sourceListModel", QVariant::fromValue(state)}};
        if (dialogName == "SourceAddDialog") properties.insert("initialName", "Theme sample");
        else {
            properties.insert("sourceId", "fixed-source-id");
            properties.insert("sourceName", "Theme sample");
        }
    }
    const QUrl url(QString("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(dialogName));
    for (int opening = 0; opening < 2; ++opening) {
        window.showDialog(url, dialogName, properties);
        QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
        QVERIFY(dialog);
        auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QPointer<QQuickItem> root = view->rootObject();
        QVERIFY(root);
        if (QGuiApplication::platformName() != "offscreen") {
            dialog->raise();
            dialog->activateWindow();
            QTRY_VERIFY(dialog->isActiveWindow());
        }
        auto *input = root->findChild<QQuickItem *>("sourceNameInput");
        const auto sourceId = root->property("sourceId");
        if (input) {
            input->setProperty("text", "Unsaved name");
            QVERIFY(QMetaObject::invokeMethod(input, "selectAll"));
            input->forceActiveFocus();
        }
        if (dialogName != "AboutDialog") root->setProperty("errorText", "Validation sample");
        for (const auto &id : {themeId, themeId == "dark" ? QString("light") : QString("dark")}) {
            QVERIFY(m_theme->setTheme(id));
            QCOMPARE(view->rootObject(), root.data());
            QCOMPARE(root->property("sourceId"), sourceId);
            QTRY_COMPARE(root->property("color").value<QColor>(), m_theme->color("background.panel"));
            if (input) {
                QCOMPARE(input->property("text").toString(), "Unsaved name");
                QCOMPARE(input->property("selectedText").toString(), "Unsaved name");
                QVERIFY(input->hasActiveFocus());
                QCOMPARE(input->property("selectionColor").value<QColor>(), m_theme->color("background.selected"));
                QCOMPARE(input->property("selectedTextColor").value<QColor>(), m_theme->color("text.primary"));
                auto *background = input->property("background").value<QQuickItem *>();
                QVERIFY(background);
                QCOMPARE(background->property("color").value<QColor>(), m_theme->color("background.input"));
                QCOMPARE(QQmlProperty::read(background, "border.color").value<QColor>(), m_theme->color("border.focus"));
            }
            for (auto *item : root->findChildren<QQuickItem *>()) {
                if (!item->isVisible() || !item->property("text").isValid() || !item->property("color").isValid()) continue;
                const auto name = item->objectName();
                bool disabledActionText = false;
                for (auto *ancestor = item; ancestor; ancestor = ancestor->parentItem()) {
                    if (ancestor->objectName() == "confirmSourceAddButton" && !ancestor->isEnabled())
                        disabledActionText = true;
                }
                const auto token = name.endsWith("Error") ? "status.error"
                    : (disabledActionText ? "text.disabled"
                    : (name == "aboutVersion" || name == "aboutQtVersion" || name == "sourceRemovalHint" || name == "sourceTypeUnavailableHint" || name == "displayCaptureHint" ? "text.secondary" : "text.primary"));
                QCOMPARE(item->property("color").value<QColor>(), m_theme->color(token));
                if (name.endsWith("Error")) QCOMPARE(item->property("text").toString(), "Validation sample");
            }
        }
        if (auto *type = root->findChild<QQuickItem *>("sourceTypeInput")) {
            auto *popup = type->property("popup").value<QObject *>();
            auto *indicator = type->property("indicator").value<QQuickItem *>();
            QVERIFY(popup && indicator);
            auto *list = popup->property("contentItem").value<QQuickItem *>();
            auto *popupBackground = popup->property("background").value<QQuickItem *>();
            QVERIFY(list && popupBackground);
            type->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyClick(view, Qt::Key_Space);
            QTRY_VERIFY(popup->property("opened").toBool());
            QTest::keyClick(view, Qt::Key_Down);
            for (const auto &id : {themeId, themeId == "dark" ? QString("light") : QString("dark")}) {
                QVERIFY(m_theme->setTheme(id));
                QVERIFY(popup->property("opened").toBool());
                QCOMPARE(type->property("currentIndex").toInt(), 0);
                QCOMPARE(indicator->property("arrowColor").value<QColor>(), m_theme->color("icon.primary"));
                QTRY_COMPARE(popupBackground->property("color").value<QColor>(), m_theme->color("background.input"));
                QQuickItem *item = nullptr;
                QTRY_VERIFY((item = list->property("currentItem").value<QQuickItem *>()));
                auto *label = item->property("contentItem").value<QQuickItem *>();
                auto *itemBackground = item->property("background").value<QQuickItem *>();
                QVERIFY(label && itemBackground);
                QTRY_COMPARE(label->property("color").value<QColor>(), m_theme->color("text.primary"));
                QTRY_COMPARE(itemBackground->property("color").value<QColor>(), m_theme->color("background.selected"));
                // Verify the Canvas actually repainted, rather than only its bound property.
                const QRect arrowRect = indicator->mapRectToScene(indicator->boundingRect()).toAlignedRect();
                const auto arrowHasThemeColor = [&] {
                    const auto frame = view->grabFramebuffer();
                    const qreal scale = qreal(frame.width()) / view->width();
                    const auto arrow = frame.copy(QRect(arrowRect.topLeft() * scale,
                        arrowRect.size() * scale)).convertToFormat(QImage::Format_RGB32);
                    for (int y = 0; y < arrow.height(); ++y)
                        for (int x = 0; x < arrow.width(); ++x)
                            if (arrow.pixelColor(x, y) == m_theme->color("icon.primary")) return true;
                    return false;
                };
                QTRY_VERIFY(arrowHasThemeColor());
                QVERIFY(view->grabFramebuffer().save(QString("theme-dialog-popup-%1.png").arg(id)));
            }
            QTest::keyClick(view, Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
            QVERIFY(dialog);
            QCOMPARE(root->property("errorText").toString(), "Validation sample");
            type->setEnabled(false);
            QCOMPARE(indicator->property("arrowColor").value<QColor>(), m_theme->color("icon.disabled"));
            type->setEnabled(true);
        }
        auto *button = root->findChild<QQuickItem *>(dialogName == "AboutDialog" ? "closeAboutButton"
            : (dialogName == "SourceAddDialog" ? "cancelSourceAddButton" : "cancelSourceRemovalButton"));
        QVERIFY(button);
        auto *background = button->property("background").value<QQuickItem *>();
        QVERIFY(background);
        button->forceActiveFocus();
        QTRY_VERIFY(button->hasActiveFocus());
        QCOMPARE(QQmlProperty::read(background, "border.color").value<QColor>(), m_theme->color("border.focus"));
        if (input) {
            auto *inputBackground = input->property("background").value<QQuickItem *>();
            QCOMPARE(QQmlProperty::read(inputBackground, "border.color").value<QColor>(), m_theme->color("border.default"));
        }
        const QPoint position = button->mapToScene(button->boundingRect().center()).toPoint();
        const auto moveMouse = [view](const QPoint &point) {
            QMouseEvent event(QEvent::MouseMove, point, view->mapToGlobal(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(view, &event);
        };
        moveMouse(QPoint(2, 2));
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.normal"));
        moveMouse(position);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.hovered"));
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, position);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.pressed"));
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, QPoint(2, 2));
        button->setEnabled(false);
        QTRY_COMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.disabled"));
        QCOMPARE(QQmlProperty::read(button, "palette.buttonText").value<QColor>(), m_theme->color("text.disabled"));
        button->setEnabled(true);
        QVERIFY(!view->grabFramebuffer().isNull());
        QVERIFY(view->grabFramebuffer().save(QString("theme-dialog-%1-%2.png").arg(dialogName, m_theme->themeId())));
        QTest::keyClick(dialog, Qt::Key_Escape);
        QTRY_VERIFY(dialog.isNull());
        QVERIFY(root.isNull());
    }
}

void MainWindowTest::dialogsShareThemeAndReleaseBeforeIt()
{
    auto window = std::make_unique<MainWindow>(*m_theme);
    window->show();
    auto *action = window->findChild<QAction *>("aboutAction");
    QVERIFY(action);
    QPointer<ThemeManager> theme = m_theme.get();
    for (int i = 0; i < 2; ++i) {
        action->trigger();
        QPointer<QDialog> dialog = window->findChild<QDialog *>("qmlDialog");
        QVERIFY(dialog);
        QPointer<QQuickWidget> view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty("uiTheme").value<QObject *>(), theme.data());
        QVERIFY(m_theme->setTheme(i == 0 ? "light" : "dark"));
        QCOMPARE(view->quickWindow()->color(), m_theme->color("background.panel"));
        QCOMPARE(dialog->palette().color(QPalette::Window), m_theme->color("background.window"));
        if (i == 0) {
            dialog->close();
            QTRY_VERIFY(dialog.isNull());
        } else {
            window.reset();
            QVERIFY(dialog.isNull());
        }
        QVERIFY(view.isNull());
        QVERIFY(theme);
        QVERIFY(!theme->parent());
    }
    // All view-owned connections have disconnected; the service remains usable.
    QVERIFY(m_theme->setTheme("light"));
}

void MainWindowTest::qmlDialogReceivesInitialPropertiesAndCloses()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("Dialog.qml")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray qml = R"(
import QtQuick
Rectangle {
    implicitWidth: 340; implicitHeight: 180
    required property string message
    property string completedMessage
    signal closeRequested()
    Component.onCompleted: completedMessage = message
    MouseArea { anchors.fill: parent; onClicked: parent.closeRequested() }
})";
    QCOMPARE(file.write(qml), qint64(qml.size()));
    file.close();
    MainWindow window(*m_theme);
    window.show();
    window.showDialog(QUrl::fromLocalFile(file.fileName()), QStringLiteral("Test dialog"),
                      {{QStringLiteral("message"), QStringLiteral("Initial value")}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    QCOMPARE(root->property("completedMessage").toString(), QStringLiteral("Initial value"));
    QCOMPARE(dialog->windowTitle(), QStringLiteral("Test dialog"));
    QTRY_VERIFY(dialog->isVisible());
    dialog->resize(400, 240);
    QTRY_COMPARE(root->size(), QSizeF(view->size()));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    QVERIFY(window.isVisible());

    window.showDialog(QUrl::fromLocalFile(file.fileName()), QStringLiteral("Reopened"),
                      {{QStringLiteral("message"), QStringLiteral("New value")}});
    dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QCOMPARE(view->rootObject()->property("completedMessage").toString(), QStringLiteral("New value"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(dialog.isNull());

    auto owner = std::make_unique<MainWindow>(*m_theme);
    owner->showDialog(QUrl::fromLocalFile(file.fileName()), QStringLiteral("Owned dialog"),
                      {{QStringLiteral("message"), QStringLiteral("Owned value")}});
    dialog = owner->findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    owner.reset();
    QVERIFY(dialog.isNull());
}

void MainWindowTest::qmlDialogShowsLoadErrors_data()
{
    QTest::addColumn<QByteArray>("qml");
    QTest::newRow("missing-file") << QByteArray();
    QTest::newRow("syntax-error") << QByteArray("import QtQuick\nRectangle {");
    QTest::newRow("missing-required-property")
        << QByteArray("import QtQuick\nItem { required property string message }");
    QTest::newRow("non-visual-root") << QByteArray("import QtQml\nQtObject {}");
}

void MainWindowTest::qmlDialogShowsLoadErrors()
{
    QFETCH(QByteArray, qml);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("Dialog.qml")));
    if (!qml.isEmpty()) {
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(qml), qint64(qml.size()));
        file.close();
    }
    MainWindow window(*m_theme);
    window.show();
    window.showDialog(QUrl::fromLocalFile(file.fileName()), QStringLiteral("Load error"));
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *errorLabel = dialog->findChild<QLabel *>(QStringLiteral("dialogLoadError"));
    QVERIFY(errorLabel);
    QTRY_VERIFY(errorLabel->isVisible());
    QVERIFY(!errorLabel->toolTip().isEmpty());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(window.isVisible());
}

void MainWindowTest::aboutActionShowsProjectInfoAndReusesDialog()
{
    MainWindow window(*m_theme);
    window.show();
    auto *action = window.findChild<QAction *>(QStringLiteral("aboutAction"));
    QVERIFY(action);
    QVERIFY(action->isEnabled());
    action->trigger();
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    QCOMPARE(root->objectName(), QStringLiteral("aboutDialogContent"));
    QCOMPARE(root->property("productName").toString(), window.windowTitle());
    QCOMPARE(root->property("qtVersion").toString(), QString::fromLatin1(qVersion()));
    const QString version = QCoreApplication::applicationVersion();
    QCOMPARE(root->property("versionText").toString(), version.isEmpty()
        ? QString::fromUtf8("开发版本（未设置版本号）") : version);
    for (const auto *name : {"aboutProductName", "aboutVersion", "aboutQtVersion"}) {
        auto *label = root->findChild<QQuickItem *>(QLatin1String(name));
        QVERIFY(label);
        QVERIFY(label->isVisible());
        QVERIFY(!label->property("text").toString().isEmpty());
    }
    action->trigger();
    QCOMPARE(window.findChildren<QDialog *>().size(), 1);
    QCOMPARE(view->rootObject(), root.data());
    dialog->resize(480, 280);
    QTRY_COMPARE(root->size(), QSizeF(view->size()));
    auto *button = root->findChild<QQuickItem *>(QStringLiteral("closeAboutButton"));
    QVERIFY(button);
    QVERIFY(button->isEnabled());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(button->boundingRect().center()).toPoint());
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    action->trigger();
    dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->isVisible());
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(window.isVisible());

    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    QVERIFY(viewMenu);
    for (auto *viewAction : viewMenu->actions()) {
        if (!viewAction->isSeparator() && !viewAction->isCheckable())
            QCOMPARE(viewAction->objectName(), QStringLiteral("resetLayoutAction"));
    }
}

void MainWindowTest::sourcePanelOpensAddDialog_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("reloaded") << true;
}

void MainWindowTest::sourcePanelOpensAddDialog()
{
    QFETCH(bool, reload);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<SourceListModel *>();
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    QVERIFY(state && panel);
    if (reload) {
        panel->setSource(QUrl("qrc:/missing-source-panel.qml"));
        QTRY_COMPARE(panel->status(), QQuickWidget::Error);
        panel->setSource(QUrl("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml"));
    }
    QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
    auto *actions = panel->rootObject()->findChild<QQuickItem *>("sourceActions");
    QVERIFY(actions);
    QQuickItem *add = nullptr;
    for (auto *item : actions->childItems()) {
        if (item->objectName() == "addSourceButton")
            add = item;
    }
    QCOMPARE(state->rowCount(), 0);
    QVERIFY(state->currentSourceId().isEmpty());
    QVERIFY(add && add->isEnabled());
    QSignalSpy requested(panel->rootObject(), SIGNAL(sourceAddDialogRequested()));
    QVERIFY(requested.isValid());
    for (int opening = 0; opening < 2; ++opening) {
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
            add->mapToScene(add->boundingRect().center()).toPoint());
        QTRY_COMPARE(window.findChildren<QDialog *>("qmlDialog").size(), 1);
        QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
        QCOMPARE(dialog->windowTitle(), QString::fromUtf8("添加来源"));
        auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        auto *root = view->rootObject();
        QVERIFY(root);
        QCOMPARE(requested.count(), opening + 1);
        QVERIFY(!root->property("sceneId").isValid());
        QVERIFY(!root->findChild<QQuickItem *>("sourceFileInput")->isVisible());
        QVERIFY(!root->findChild<QQuickItem *>("browseSourceFileButton"));
        QVERIFY(!dialog->findChild<QObject *>("imageFileDialog"));
        auto *name = root->findChild<QQuickItem *>("sourceNameInput");
        QVERIFY(name);
        QCOMPARE(name->property("text").toString(), QString::fromUtf8("显示器采集"));
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(state->rowCount(), 0);
    }
}

void MainWindowTest::addsImageThroughDialog()
{
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(80, 40, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *model = window.findChild<SourceListModel *>();
    QVERIFY(model);
    const auto open = [&] {
        window.showDialog(QUrl("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml"), "Add image",
            {{"sourceListModel", QVariant::fromValue(model)}});
    };
    open();
    QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *root = view->rootObject();
    auto *type = root->findChild<QQuickItem *>("sourceTypeInput");
    auto *name = root->findChild<QQuickItem *>("sourceNameInput");
    auto *input = root->findChild<QQuickItem *>("sourceFileInput");
    auto *confirm = root->findChild<QQuickItem *>("confirmSourceAddButton");
    QVERIFY(type && name && input && confirm);
    type->setProperty("currentIndex", 3);
    QVERIFY(input->isVisible());
    QVERIFY(confirm->isEnabled());
    name->setProperty("text", "Logo");
    input->setProperty("text", dir.filePath("missing.png"));
    QVERIFY(QMetaObject::invokeMethod(root, "submit"));
    QVERIFY(root->property("loading").toBool());
    QVERIFY(!confirm->isEnabled());
    QTRY_VERIFY(!root->property("loading").toBool());
    QVERIFY(!root->property("errorText").toString().isEmpty());
    QVERIFY(dialog->isVisible());
    QCOMPARE(model->rowCount(), 0);
    // Browsing cancellation must preserve an already entered path.
    input->setProperty("text", path);
    auto *browse = root->findChild<QQuickItem *>("browseSourceImageButton");
    QVERIFY(browse);
    const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QTimer::singleShot(0, [&] {
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *file = qobject_cast<QFileDialog *>(widget)) file->reject();
    });
    QVERIFY(QMetaObject::invokeMethod(browse, "clicked"));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QCOMPARE(input->property("text").toString(), path);
    QVERIFY(QMetaObject::invokeMethod(root, "submit"));
    QVERIFY(root->property("loading").toBool());
    // Duplicate submission is ignored while decoding.
    QVERIFY(QMetaObject::invokeMethod(root, "submit"));
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data(SourceListModel::NameRole).toString(), QString("Logo"));
    QCOMPARE(model->index(0, 0).data(SourceListModel::OriginalSizeRole).toSize(), image.size());
}

void MainWindowTest::cancelsPendingImageThroughDialog_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *method : {"cancel", "escape", "close"})
        QTest::newRow(method) << QString::fromLatin1(method);
}

void MainWindowTest::cancelsPendingImageThroughDialog()
{
    QFETCH(QString, method);
    QTemporaryDir dir;
    const QString path = dir.filePath("image.png");
    QImage image(80, 40, QImage::Format_RGBA8888);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *model = window.findChild<SourceListModel *>();
    auto *controller = window.findChild<ImageSourceController *>();
    window.showDialog(QUrl("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml"), "Add image",
        {{"sourceListModel", QVariant::fromValue(model)}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *root = view->rootObject();
    QTRY_VERIFY(root->findChild<QQuickItem *>("sourceNameInput")->hasActiveFocus());
    QTRY_VERIFY(dialog->isActiveWindow());
    root->findChild<QQuickItem *>("sourceTypeInput")->setProperty("currentIndex", 3);
    root->findChild<QQuickItem *>("sourceFileInput")->setProperty("text", path);
    root->findChild<QQuickItem *>("sourceNameInput")->setProperty("text", "Cancelled");
    QVERIFY(QMetaObject::invokeMethod(root, "submit"));
    QVERIFY(root->property("loading").toBool());
    if (method == "cancel") {
        QVERIFY(QMetaObject::invokeMethod(root->findChild<QQuickItem *>("cancelSourceAddButton"), "clicked"));
    } else if (method == "escape") {
        QTest::keyClick(view, Qt::Key_Escape);
    } else {
        dialog->close();
    }
    QVERIFY(!dialog->isVisible());
    QTRY_VERIFY(dialog.isNull());
    QSignalSpy added(controller, &ImageSourceController::imageAdded);
    QObject owner;
    controller->addImage("Kept", path, &owner);
    QTRY_COMPARE(added.count(), 1);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data(SourceListModel::NameRole).toString(), QString("Kept"));
}

void MainWindowTest::sourceAddDialogHandlesInput_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *method : {"cancel", "escape", "close"})
        QTest::newRow(method) << QString::fromLatin1(method);
}

void MainWindowTest::sourceAddDialogHandlesInput()
{
    QFETCH(QString, method);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<SourceListModel *>();
    QVERIFY(state);
    window.showDialog(QUrl("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml"), "Add source",
        {{"sourceListModel", QVariant::fromValue(state)}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    auto *name = root->findChild<QQuickItem *>("sourceNameInput");
    auto *confirm = root->findChild<QQuickItem *>("confirmSourceAddButton");
    auto *cancel = root->findChild<QQuickItem *>("cancelSourceAddButton");
    QVERIFY(name && confirm && cancel);
    QTRY_VERIFY(name->hasActiveFocus());
    QCOMPARE(name->property("selectedText").toString(), name->property("text").toString());
    QTest::keyClicks(view, "Custom source");
    root->findChild<QQuickItem *>("sourceTypeInput")->setProperty("currentIndex", 1);
    QVERIFY(!confirm->isEnabled());
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
        confirm->mapToScene(confirm->boundingRect().center()).toPoint());
    QVERIFY(dialog && dialog->isVisible());
    QCOMPARE(state->rowCount(), 0);
    if (method == "cancel") {
        name->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_Tab);
        QTRY_VERIFY(cancel->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Space);
    } else if (method == "escape") {
        QTest::keyClick(view, Qt::Key_Escape);
    } else {
        dialog->close();
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    QCOMPARE(state->rowCount(), 0);
}

void MainWindowTest::sourceAddDialogHandlesTypeSelection_data()
{
    QTest::addColumn<QString>("themeId");
    QTest::newRow("dark") << QString("dark");
    QTest::newRow("light") << QString("light");
}

void MainWindowTest::sourceAddDialogHandlesTypeSelection()
{
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme);
    window.show();
    auto *manager = window.findChild<SourceListModel *>();
    QVERIFY(manager);
    window.showDialog(QUrl("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml"), "Add source",
        {{"sourceListModel", QVariant::fromValue(manager)}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>("qmlDialog");
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *root = view->rootObject();
    QVERIFY(root);
    auto *type = root->findChild<QQuickItem *>("sourceTypeInput");
    auto *name = root->findChild<QQuickItem *>("sourceNameInput");
    auto *confirm = root->findChild<QQuickItem *>("confirmSourceAddButton");
    auto *hint = root->findChild<QQuickItem *>("sourceTypeUnavailableHint");
    QVERIFY(type && name && confirm && hint);
    QCOMPARE(type->property("count").toInt(), 5);
    QCOMPARE(type->property("currentValue").toString(), QString("displayCapture"));
    QCOMPARE(name->property("text").toString(), QString::fromUtf8("显示器采集"));
    const auto activateType = [&](int index) {
        type->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_Space);
        auto *popup = type->property("popup").value<QObject *>();
        QVERIFY(popup);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(view, Qt::Key_Home);
        for (int row = 0; row < index; ++row)
            QTest::keyClick(view, Qt::Key_Down);
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(type->property("currentIndex").toInt(), index);
        QTRY_VERIFY(!popup->property("visible").toBool());
    };
    const QStringList keys{"displayCapture", "windowCapture", "camera", "image", "text"};
    const QStringList labels{QString::fromUtf8("显示器采集"), QString::fromUtf8("窗口采集"),
                             QString::fromUtf8("摄像头"), QString::fromUtf8("图片"), QString::fromUtf8("文字")};
    for (int index = 0; index < keys.size(); ++index) {
        root->setProperty("errorText", "Previous error");
        activateType(index);
        QCOMPARE(type->property("currentValue").toString(), keys.at(index));
        QVERIFY(root->property("errorText").toString().isEmpty());
        QCOMPARE(hint->isVisible(), index != 3 && index != 0 && index != 2);
        QCOMPARE(hint->property("color").value<QColor>(), m_theme->color("text.secondary"));
        QPointer<QQuickItem> imageInput = root->findChild<QQuickItem *>("sourceFileInput");
        QVERIFY(imageInput);
        QCOMPARE(imageInput->isVisible(), index == 3);
        const auto displaySettings = root->findChild<QQuickItem *>("displaySettings");
        QVERIFY(displaySettings);
        if (index != 2)
            QCOMPARE(confirm->isEnabled(), index == 3 || (index == 0 && !displaySettings->property("displayId").toString().isEmpty()));
        auto *background = confirm->property("background").value<QQuickItem *>();
        QVERIFY(background);
        if (!confirm->isEnabled())
            QCOMPARE(background->property("color").value<QColor>(), m_theme->color("button.background.disabled"));
        QCOMPARE(type->property("currentText").toString(), labels.at(index));
        QCOMPARE(name->property("text").toString(), labels.at(index));
        if (index == 0 || index == 2) continue; // Capture submission has its own UI tests.
        // A direct call or Return in the name field must also reject these types.
        QVERIFY(QMetaObject::invokeMethod(root, "submit"));
        QVERIFY(!root->property("errorText").toString().isEmpty());
        name->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_Return);
        QCOMPARE(manager->rowCount(), 0);
        QVERIFY(dialog && dialog->isVisible());
    }
    name->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(view, "Custom source");
    for (int index : {1, 2, 3, 4, 0}) {
        activateType(index);
        QCOMPARE(name->property("text").toString(), QString("Custom source"));
    }
    QVERIFY(!hint->isVisible());
    QCOMPARE(confirm->isEnabled(), !root->findChild<QQuickItem *>("displaySettings")->property("displayId").toString().isEmpty());
    auto *cancel = root->findChild<QQuickItem *>("cancelSourceAddButton");
    QVERIFY(cancel);
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
        cancel->mapToScene(cancel->boundingRect().center()).toPoint());
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(manager->rowCount(), 0);
}

void MainWindowTest::sourcePanelRequestsRemoval_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("reloaded") << true;
}

void MainWindowTest::sourcePanelRequestsRemoval()
{
    QFETCH(bool, reload);
    const QString first = "alpha", second = "beta";
    SourceListModel state(QList<SourceData>{{second, "Beta"}, {first, "Alpha"}});
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.rootContext()->setContextProperty("sourceListModel", &state);
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(280, 260);
    const QUrl url("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml");
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    if (reload) {
        panel.setSource(QUrl("qrc:/missing-source-removal-panel.qml"));
        QTRY_COMPARE(panel.status(), QQuickWidget::Error);
        panel.setSource(url);
        QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    }
    const auto removeButton = [&]() -> QQuickItem * {
        auto *actions = panel.rootObject()->findChild<QQuickItem *>("sourceActions");
        if (actions) {
            for (auto *item : actions->childItems()) {
                if (item->objectName() == "removeSourceButton") return item;
            }
        }
        return nullptr;
    };
    // Finish the new root's first layout before exercising model changes.
    QVERIFY(!panel.grabFramebuffer().isNull());
    QSignalSpy requested(panel.rootObject(), SIGNAL(sourceRemovalDialogRequested(QString)));
    QVERIFY(requested.isValid());
    const auto clickRemove = [&] {
        auto *button = removeButton();
        QVERIFY(button);
        QTest::mouseClick(&panel, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
    };
    QVERIFY(removeButton());
    QVERIFY(!removeButton()->isEnabled());
    clickRemove();
    QCOMPARE(requested.count(), 0);
    for (const auto &id : {first, second}) {
        QVERIFY(state.selectSource(id));
        QTRY_VERIFY(removeButton()->isEnabled());
        requested.clear();
        clickRemove();
        QTRY_COMPARE(requested.count(), 1);
        QCOMPARE(requested.first().size(), 1);
        QCOMPARE(requested.first().at(0).toString(), id);
        // Requesting confirmation leaves the entry intact.
        QCOMPARE(state.currentSourceId(), id);
        QVERIFY(state.removeSource(id));
        QTRY_VERIFY(!removeButton()->isEnabled());
    }
    QCOMPARE(state.rowCount(), 0);
    auto *empty = panel.rootObject()->findChild<QQuickItem *>("emptySourcesLabel");
    QVERIFY(empty);
    QTRY_VERIFY(empty->isVisible());
    requested.clear();
    clickRemove();
    QCOMPARE(requested.count(), 0);
}

void MainWindowTest::sourceRemovalDialogHandlesConfirmation_data()
{
    QTest::addColumn<QString>("action");
    for (const auto *name : {"confirm", "last", "cancel", "escape", "close", "stale-source", "invalid-source"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void MainWindowTest::sourceRemovalDialogHandlesConfirmation()
{
    QFETCH(QString, action);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("image.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    const QString name = "<b>Logo & image</b>";
    QList<SourceData> sources{
        {"shared", name, ImageConfig{path}, {image.size()}}};
    if (action != "last") sources.append(SourceData{"remaining", "Remaining"});
    SourceListModel state(sources);
    QVERIFY(state.selectSource("shared"));
    MainWindow window(*m_theme);
    window.show();
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")),
        QStringLiteral("Remove source"), {{"sourceListModel", QVariant::fromValue(&state)},
        {"sourceId", action == "invalid-source" ? QString("missing") : QString("shared")}, {"sourceName", name}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    QVERIFY(!root->property("sceneId").isValid());
    auto *message = root->findChild<QQuickItem *>(QStringLiteral("sourceRemovalMessage"));
    auto *confirm = root->findChild<QQuickItem *>(QStringLiteral("confirmSourceRemovalButton"));
    auto *cancel = root->findChild<QQuickItem *>(QStringLiteral("cancelSourceRemovalButton"));
    auto *error = root->findChild<QQuickItem *>(QStringLiteral("sourceRemovalError"));
    QVERIFY(message && confirm && cancel && error);
    QVERIFY(message->property("text").toString().contains(name));
    QCOMPARE(message->property("textFormat").toInt(), 0); // Text.PlainText
    QTRY_VERIFY(cancel->hasActiveFocus());
    QCOMPARE(state.rowCount(), sources.size());
    if (action == "stale-source")
        QVERIFY(state.removeSource("shared"));
    const auto click = [&](QQuickItem *button) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
    };
    if (action == "cancel") {
        QTest::keyClick(view, Qt::Key_Space); // Cancel is the initial keyboard action.
    } else if (action == "escape") {
        QTest::keyClick(view, Qt::Key_Escape);
    } else if (action == "close") {
        dialog->close();
    } else {
        click(confirm);
        if (action == "stale-source" || action == "invalid-source") {
            QTRY_VERIFY(!error->property("text").toString().isEmpty());
            QVERIFY(dialog->isVisible());
            click(cancel);
        }
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    const bool removed = action == "confirm" || action == "last" || action == "stale-source";
    QCOMPARE(state.rowCount(), sources.size() - (removed ? 1 : 0));
    if (removed) {
        QVERIFY(!state.selectSource("shared"));
        QVERIFY(state.currentSourceId().isEmpty());
    } else {
        QCOMPARE(state.currentSourceId(), QString("shared"));
    }
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
}

void MainWindowTest::keepsOutputStatusVisibleWhenResized()
{
    MainWindow window(*m_theme);
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

void MainWindowTest::loadsSourcePanelWithEmptyState()
{
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
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
    AudioSourceModel audio;
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.rootContext()->setContextProperty("audioSourceModel", &audio);
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    panel.setSource(QUrl("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml"));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject(); QVERIFY(root);
    auto *empty = findQuickItem(root, "emptySoundSourcesLabel"); QVERIFY(empty && empty->isVisible());
    auto *add = findQuickItem(root, "addSoundSourceButton"); QVERIFY(add && add->isEnabled());
    const auto first = audio.addSource("Mic", "microphone", "missing-ui-mic");
    const auto second = audio.addSource("Desktop", "system", "missing-ui-system");
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    QTRY_VERIFY(findQuickItem(root, "soundChannel_" + first));
    QVERIFY(!empty->isVisible());
    auto *slider = findQuickItem(root, first + "VolumeSlider"); QVERIFY(slider && slider->isEnabled());
    QCOMPARE(slider->property("value").toDouble(), 1.0);
    slider->setProperty("value", .25);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QCOMPARE(audio.source(first)["volume"].toDouble(), .25);
    auto *mute = findQuickItem(root, first + "MuteButton"); QVERIFY(mute && mute->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(mute, "clicked"));
    QVERIFY(audio.source(first)["muted"].toBool());
    QVERIFY(QQmlProperty::read(mute,"icon.source").toUrl().toString().endsWith("volume-x.svg"));
    for (const auto &size : {QSize(160,140), QSize(238,178), QSize(400,500)}) {
        panel.resize(size); QTRY_COMPARE(root->size(), QSizeF(size));
        auto *a = findQuickItem(root, "soundChannel_" + first);
        auto *b = findQuickItem(root, "soundChannel_" + second); QVERIFY(a && b);
        QVERIFY(!a->mapRectToScene(a->boundingRect()).intersects(b->mapRectToScene(b->boundingRect())));
    }
    auto *scroll = findQuickItem(root,"mixerScrollView"); QVERIFY(scroll);
    panel.resize(160,140);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
    auto *bar = findQuickItem(root,"mixerVerticalScrollBar"); QVERIFY(bar);
    QTRY_VERIFY(bar->height() > 20);
    QVERIFY(bar->x() > scroll->width() / 2);
    QVERIFY(audio.removeSource(first)); QVERIFY(audio.removeSource(second));
    QTRY_VERIFY(empty->isVisible());
}

void MainWindowTest::soundSourcesAndDialogs()
{
    MainWindow window(*m_theme); window.show();
    auto *audio = window.findChild<AudioSourceModel *>(); QVERIFY(audio);
    QCOMPARE(audio->count(), 0);
    QCOMPARE(window.findChild<QDockWidget *>("sourcesDock")->windowTitle(), QString::fromUtf8("画面来源"));
    QCOMPARE(window.findChild<QDockWidget *>("mixerDock")->windowTitle(), QString::fromUtf8("声音来源"));
    auto *panel = window.findChild<QQuickWidget *>("mixerQuickWidget"); QVERIFY(panel && panel->rootObject());
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "soundSourceAddDialogRequested"));
    QPointer<QDialog> dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    auto *root = view->rootObject();
    if (QGuiApplication::platformName() != "offscreen") {
        QTRY_VERIFY(!audio->refreshing());
        QVERIFY(dialog->grab().save(QCoreApplication::applicationDirPath()+"/sound-add-dialog.png"));
    }
    auto *name = root->findChild<QQuickItem *>("soundNameInput"); QVERIFY(name);
    name->setProperty("text", "Microphone"); root->setProperty("deviceId", "missing-dialog-device");
    auto *confirm = root->findChild<QQuickItem *>("confirmSoundSourceButton"); QVERIFY(confirm && confirm->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(confirm,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(audio->count(),1);
    const QString id = audio->currentSourceId();
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "soundSourcePropertiesDialogRequested", Q_ARG(QString,id)));
    dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    root = view->rootObject();
    if (QGuiApplication::platformName() != "offscreen")
        QVERIFY(dialog->grab().save(QCoreApplication::applicationDirPath()+"/sound-properties-dialog.png"));
    name = root->findChild<QQuickItem *>("soundNameInput"); QVERIFY(name);
    name->setProperty("text", "Canceled rename");
    auto *cancel = root->findChild<QQuickItem *>("cancelSoundSourceButton"); QVERIFY(cancel);
    QVERIFY(QMetaObject::invokeMethod(cancel,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(audio->source(id)["name"].toString(),QString("Microphone"));
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "soundSourcePropertiesDialogRequested", Q_ARG(QString,id)));
    dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    name = view->rootObject()->findChild<QQuickItem *>("soundNameInput"); QVERIFY(name);
    name->setProperty("text", "Renamed microphone");
    confirm = view->rootObject()->findChild<QQuickItem *>("confirmSoundSourceButton"); QVERIFY(confirm);
    QVERIFY(QMetaObject::invokeMethod(confirm,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(audio->source(id)["name"].toString(),QString("Renamed microphone"));
    auto *pictures = window.findChild<SourceListModel *>(); QVERIFY(pictures);
    QVERIFY(pictures->addSource({"camera-audio-independent", "Camera", CameraConfig{"missing-independent-camera"}}));
    QVERIFY(pictures->setSourceVisible("camera-audio-independent",false));
    QVERIFY(pictures->removeSource("camera-audio-independent")); QCOMPARE(audio->count(),1);
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(),"soundSourceRemovalDialogRequested",Q_ARG(QString,id)));
    dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    auto *remove = view->rootObject()->findChild<QQuickItem *>("confirmSourceRemovalButton"); QVERIFY(remove);
    QVERIFY(QMetaObject::invokeMethod(remove,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(audio->count(),0); QTRY_COMPARE(audio->activeWorkerCount(),0);
}

void MainWindowTest::soundHardwarePreview()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_AUDIO_HARDWARE")) QSKIP("Requires explicit WASAPI hardware validation");
    MainWindow window(*m_theme); window.show();
    auto *audio = window.findChild<AudioSourceModel *>(); QVERIFY(audio);
    audio->refreshDevices(); QTRY_VERIFY(!audio->refreshing());
    QString microphone, system;
    for (const auto &value : audio->devices()) {
        const auto device = value.toMap();
        if (device["type"] == "microphone" && microphone.isEmpty()) microphone = device["id"].toString();
        if (device["type"] == "system" && system.isEmpty()) system = device["id"].toString();
    }
    QVERIFY(!microphone.isEmpty() && !system.isEmpty());
    const auto mic = audio->addSource(QString::fromUtf8("麦克风"),"microphone",microphone);
    const auto desktop = audio->addSource(QString::fromUtf8("系统声音"),"system",system);
    QVERIFY(!mic.isEmpty() && !desktop.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(audio->source(mic)["state"].toString(),QString("capturing"),5000);
    QTRY_COMPARE_WITH_TIMEOUT(audio->source(desktop)["state"].toString(),QString("capturing"),5000);
    auto *panel = window.findChild<QQuickWidget *>("mixerQuickWidget"); QVERIFY(panel && panel->rootObject());
    auto *slider = findQuickItem(panel->rootObject(),mic+"VolumeSlider"); QVERIFY(slider);
    slider->setProperty("value",.5); QVERIFY(QMetaObject::invokeMethod(slider,"moved"));
    QCOMPARE(audio->source(mic)["volume"].toDouble(),.5);
    auto *mute = findQuickItem(panel->rootObject(),mic+"MuteButton"); QVERIFY(mute);
    QVERIFY(QMetaObject::invokeMethod(mute,"clicked")); QVERIFY(audio->source(mic)["muted"].toBool());
    QTest::qWait(200);
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()+"/sound-hardware-mainwindow.png"));
    window.showMinimized(); QCoreApplication::processEvents();
    const auto sequence = audio->mixer()->latestBlock()->sequence;
    QThread::msleep(150);
    QVERIFY(audio->mixer()->latestBlock()->sequence > sequence + 5);
    QCOMPARE(audio->source(desktop)["state"].toString(),QString("capturing"));
    window.showNormal();
    QVERIFY(audio->removeSource(mic)); QVERIFY(audio->removeSource(desktop));
    QTRY_COMPARE_WITH_TIMEOUT(audio->activeWorkerCount(),0,2000);
}

void MainWindowTest::outputSettingsDialogDraftsAndTheme()
{
    MainWindow window(*m_theme); window.show();
    auto *settings=window.findChild<OutputSettings *>(); QVERIFY(settings);
    auto *controls=window.findChild<QQuickWidget *>("controlsQuickWidget"); QVERIFY(controls && controls->rootObject());
    auto *button=controls->rootObject()->findChild<QQuickItem *>("settingsButton"); QVERIFY(button && button->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
    QPointer<QDialog> dialog=window.findChild<QDialog *>(); QVERIFY(dialog);
    auto *view=dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    auto *root=view->rootObject();
    QVERIFY(QMetaObject::invokeMethod(controls->rootObject(),"settingsDialogRequested"));
    QCOMPARE(window.findChildren<QDialog *>().size(),1);
    QTemporaryDir directory;
    const auto defaults=settings->recordingDirectory();
    root->setProperty("directory",directory.path()); root->setProperty("quality",2);
    root->setProperty("server","rtmps://example.test/live"); root->setProperty("key","unit-test-secret");
    auto *key=root->findChild<QQuickItem *>("streamKeyInput"); QVERIFY(key);
    QCOMPARE(key->property("echoMode").toInt(),2);
    for (const auto &theme : {QString("dark"),QString("light")}) {
        QVERIFY(m_theme->setTheme(theme));
        QTRY_COMPARE(root->property("color").value<QColor>(),m_theme->color("background.panel"));
        if (QGuiApplication::platformName()!="offscreen")
            QVERIFY(view->grabFramebuffer().save(QCoreApplication::applicationDirPath()+"/output-settings-"+theme+".png"));
    }
    auto *cancel=root->findChild<QQuickItem *>("cancelOutputSettingsButton"); QVERIFY(cancel);
    QVERIFY(QMetaObject::invokeMethod(cancel,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(settings->recordingDirectory(),defaults); QVERIFY(settings->streamKey().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(button,"clicked"));
    dialog=window.findChild<QDialog *>(); QVERIFY(dialog);
    view=dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject()); root=view->rootObject();
    auto *apply=root->findChild<QQuickItem *>("applyOutputSettingsButton"); QVERIFY(apply);
    root->setProperty("directory",""); QVERIFY(QMetaObject::invokeMethod(apply,"clicked"));
    QVERIFY(dialog); QVERIFY(!root->property("errorText").toString().isEmpty());
    root->setProperty("directory",directory.path()); root->setProperty("quality",0);
    root->setProperty("server","rtmp://example.test/app"); root->setProperty("key","memory-only-test-key");
    QVERIFY(QMetaObject::invokeMethod(apply,"clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(settings->recordingDirectory(),directory.path()); QCOMPARE(settings->quality(),0);
    QCOMPARE(settings->streamKey(),QString("memory-only-test-key"));
    OutputSettings fresh; QVERIFY(fresh.streamKey().isEmpty()); QCOMPARE(fresh.recordingDirectory(),defaults);
}

void MainWindowTest::recordingControlsAndGracefulClose()
{
    MainWindow window(*m_theme); window.show();
    auto *recorder=window.findChild<RecordingController *>();
    if (!recorder) QSKIP("Native desktop OpenGL required");
    QTRY_VERIFY_WITH_TIMEOUT(recorder->ready(),5000);
    auto *settings=window.findChild<OutputSettings *>(); QVERIFY(settings);
    QTemporaryDir directory; QVERIFY(settings->apply(directory.path(),1,"",""));
    auto *controls=window.findChild<QQuickWidget *>("controlsQuickWidget"); QVERIFY(controls && controls->rootObject());
    auto *record=controls->rootObject()->findChild<QQuickItem *>("recordButton"); QVERIFY(record);
    QTRY_VERIFY(record->isEnabled()); QVERIFY(QMetaObject::invokeMethod(record,"clicked"));
    QTRY_COMPARE_WITH_TIMEOUT(recorder->state(),QString("recording"),5000);
    QVERIFY(settings->recordingLocked()); QCOMPARE(record->property("text").toString(),QString::fromUtf8("停止录制"));
    QVERIFY(QMetaObject::invokeMethod(controls->rootObject(),"settingsDialogRequested"));
    QPointer<QDialog> dialog=window.findChild<QDialog *>(); QVERIFY(dialog);
    auto *view=dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    auto *root=view->rootObject();
    QVERIFY(!root->findChild<QQuickItem *>("recordingDirectoryInput")->isEnabled());
    QVERIFY(!root->findChild<QQuickItem *>("recordingQualityInput")->isEnabled());
    QVERIFY(!settings->apply(directory.filePath("other"),2,"",""));
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QQuickItem *>("cancelOutputSettingsButton"),"clicked"));
    QTRY_VERIFY(dialog.isNull());
    window.showMinimized(); QTest::qWait(350);
    QCOMPARE(recorder->state(),QString("recording"));
    QVERIFY(!window.close());
    QTRY_VERIFY_WITH_TIMEOUT(!recorder->busy(),10000);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY2(recorder->lastError().isEmpty(),qPrintable(recorder->lastError()));
    QVERIFY(recorder->fileComplete()); QVERIFY(QFileInfo(recorder->filePath()).size()>1000);
    QVERIFY(!settings->recordingLocked());
}

void MainWindowTest::recordingAcceptance()
{
    if (qEnvironmentVariableIntValue("OPENLIVE_RECORDING_SOAK_SECONDS")<=0)
        QSKIP("Explicit real-time recording acceptance only");
    MainWindow window(*m_theme);
    RecordingAcceptance::run(window);
}

void MainWindowTest::loadsControlsPanelWithVerticalButtons()
{
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(286, 174);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ControlsPanel.qml")));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    auto *root = panel.rootObject();
    QVERIFY(root);
    auto *scroll = root->findChild<QQuickItem *>(QStringLiteral("controlsScrollView"));
    auto *actions = root->findChild<QQuickItem *>(QStringLiteral("controlActions"));
    auto *spacer = root->findChild<QQuickItem *>(QStringLiteral("controlsSpacer"));
    QVERIFY(scroll);
    QVERIFY(actions);
    QVERIFY(spacer);
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
            QCOMPARE(rect.height(), qreal(32));
            if (!previous.isNull()) {
                QVERIFY(previous.bottom() < rect.top());
                QCOMPARE(rect.left(), previous.left());
                QCOMPARE(rect.width(), previous.width());
                QVERIFY(qAbs(rect.height() - previous.height()) <= 1);
            }
            previous = rect;
        }
        if (size.height() > 130) {
            QVERIFY(spacer->height() > 0);
            QVERIFY(spacer->mapRectToScene(spacer->boundingRect()).top() > previous.bottom());
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

void MainWindowTest::embedsSourcePanelInFullHeightLeftDock()
{
    MainWindow window(*m_theme);
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("sourcesQuickWidget"));
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
        QTRY_COMPARE(dock->geometry().top(), window.centralWidget()->geometry().top());
        auto *mixer = window.findChild<QDockWidget *>(QStringLiteral("mixerDock"));
        QVERIFY(mixer);
        QTRY_COMPARE(dock->geometry().bottom(), mixer->geometry().bottom());
        QVERIFY(!dock->geometry().intersects(window.centralWidget()->geometry()));
        QTRY_COMPARE(view->rootObject()->size(), QSizeF(view->size()));
        auto *actions = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceActions"));
        QVERIFY(actions);
        for (auto *button : actions->childItems()) {
            if (!button->objectName().endsWith(QStringLiteral("Button")))
                continue;
            QCOMPARE(button->isEnabled(), button->objectName()==QStringLiteral("addSourceButton"));
            QVERIFY(view->rootObject()->boundingRect().contains(
                button->mapRectToScene(button->boundingRect())));
        }
    }
}

void MainWindowTest::embedsMixerPanelBelowPreview()
{
    MainWindow window(*m_theme);
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("mixerDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("mixerQuickWidget"));
    auto *errorLabel = window.findChild<QLabel *>(QStringLiteral("mixerLoadError"));
    QVERIFY(dock);
    QVERIFY(view);
    QVERIFY(errorLabel);
    QCOMPARE(window.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
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
        QTRY_VERIFY(dock->geometry().top() > window.centralWidget()->geometry().bottom());
        QVERIFY(window.centralWidget()->width() > 0);
        QVERIFY(window.rect().contains(dock->geometry()));
    }
}

void MainWindowTest::hasNoTransitionsDock()
{
    MainWindow window(*m_theme);
    window.show();
    QCOMPARE(window.findChildren<QDockWidget *>().size(), 3);
    QVERIFY(!window.findChild<QDockWidget *>(QStringLiteral("transitionsDock")));
    QVERIFY(!window.findChild<QQuickWidget *>(QStringLiteral("transitionsQuickWidget")));
    QVERIFY(!window.findChild<QAction *>(QStringLiteral("transitionsDockViewAction")));
    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    QVERIFY(viewMenu);
    QCOMPARE(viewMenu->actions().size(), 5); // Three docks, separator and reset.
}

void MainWindowTest::embedsControlsPanelAtBottomRight()
{
    MainWindow window(*m_theme);
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
            QVERIFY(otherDock->geometry().right() < dock->geometry().left());
            if (window.dockWidgetArea(otherDock) == Qt::BottomDockWidgetArea) {
                QCOMPARE(otherDock->geometry().top(), dock->geometry().top());
                QCOMPARE(otherDock->geometry().bottom(), dock->geometry().bottom());
            } else {
                QVERIFY(otherDock->geometry().left() < window.centralWidget()->geometry().left());
            }
        }
        for (const auto *name : {"recordButton", "streamButton", "settingsButton"}) {
            auto *button = view->rootObject()->findChild<QQuickItem *>(QLatin1String(name));
            QVERIFY(button);
            const auto *recorder=window.findChild<RecordingController *>();
            QCOMPARE(button->isEnabled(), button->objectName()=="settingsButton"
                || (button->objectName()=="recordButton" && recorder && recorder->ready()));
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
    QTest::newRow("sources") << QStringLiteral("sources")
        << Qt::LeftDockWidgetArea << QStringLiteral("SourcesPanel");
    QTest::newRow("mixer") << QStringLiteral("mixer")
        << Qt::BottomDockWidgetArea << QStringLiteral("MixerPanel");
    QTest::newRow("controls") << QStringLiteral("controls")
        << Qt::BottomDockWidgetArea << QStringLiteral("ControlsPanel");
}

void MainWindowTest::panelDockCanFloatRedockAndClose()
{
    QFETCH(QString, panelId);
    QFETCH(Qt::DockWidgetArea, dockArea);
    MainWindow window(*m_theme);
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
    MainWindow window(*m_theme);
    window.show();
    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    QVERIFY(viewMenu);
    const auto docks = window.findChildren<QDockWidget *>();
    QCOMPARE(docks.size(), 3);
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
    MainWindow window(*m_theme);
    window.resize(windowSize);
    window.show();
    auto *resetAction = window.findChild<QAction *>(QStringLiteral("resetLayoutAction"));
    QVERIFY(resetAction);
    QVERIFY(resetAction->isEnabled());
    QList<QDockWidget *> docks;
    QList<QQuickWidget *> views;
    QList<QPointer<QQuickItem>> roots;
    const QList<Qt::DockWidgetArea> areas{Qt::LeftDockWidgetArea,
        Qt::BottomDockWidgetArea, Qt::BottomDockWidgetArea};
    for (const auto *id : {"sources", "mixer", "controls"}) {
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
            window.addDockWidget(Qt::RightDockWidgetArea, docks[0]);
            window.tabifyDockWidget(docks[1], docks[0]);
            QVERIFY(!window.tabifiedDockWidgets(docks[1]).isEmpty());
            docks[2]->setFloating(true);
            docks[2]->move(-10000, -10000);
            docks[0]->close();
            docks[2]->close();
        } else if (pass == 1) {
            for (auto *dock : docks) {
                dock->setFloating(true);
                dock->close();
            }
        }
        resetAction->trigger();
        QTRY_COMPARE(window.size(), windowSize);
        QCOMPARE(window.findChildren<QDockWidget *>().size(), 3);
        QCOMPARE(window.findChildren<QQuickWidget *>().size(), 3);
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
        QTRY_COMPARE(docks[0]->geometry().top(), window.centralWidget()->geometry().top());
        QTRY_VERIFY(docks[1]->geometry().right() < docks[2]->geometry().left());
        for (int i = 1; i < docks.size(); ++i) {
            QVERIFY(docks[i]->geometry().top() > window.centralWidget()->geometry().bottom());
            QCOMPARE(docks[i]->geometry().top(), docks[1]->geometry().top());
            QCOMPARE(docks[i]->geometry().bottom(), docks[0]->geometry().bottom());
            QVERIFY(docks[0]->geometry().right() < docks[i]->geometry().left());
        }
        QVERIFY(docks[0]->geometry().right() < window.centralWidget()->geometry().left());
        QVERIFY(window.centralWidget()->width() > 0);
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
    MainWindow window(*m_theme);
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
                             "eye", "eye-off", "lock", "lock-open", "sliders-horizontal", "settings", "volume-2", "arrow-right",
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
    QTest::addColumn<bool>("alternateSourceState");
    QTest::addColumn<QString>("themeId");
    QTest::newRow("sources-dark") << QStringLiteral("SourcesPanel") << 8 << false << QString("dark");
    QTest::newRow("sources-light") << QStringLiteral("SourcesPanel") << 8 << false << QString("light");
    QTest::newRow("sources-hidden-locked-dark") << QStringLiteral("SourcesPanel") << 8 << true << QString("dark");
    QTest::newRow("sources-hidden-locked-light") << QStringLiteral("SourcesPanel") << 8 << true << QString("light");
    QTest::newRow("mixer-dark") << QStringLiteral("MixerPanel") << 3 << false << QString("dark");
    QTest::newRow("mixer-light") << QStringLiteral("MixerPanel") << 3 << false << QString("light");
    QTest::newRow("controls-dark") << QStringLiteral("ControlsPanel") << 2 << false << QString("dark");
    QTest::newRow("controls-light") << QStringLiteral("ControlsPanel") << 2 << false << QString("light");
}

void MainWindowTest::panelIconsRenderInDisabledAndEnabledStates()
{
    QFETCH(QString, panelName);
    QFETCH(int, expectedButtons);
    QFETCH(bool, alternateSourceState);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 260);
    if (panelName == QStringLiteral("ControlsPanel"))
        panel.resize(286, 174);
    panel.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/%1.qml").arg(panelName)));
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    panel.show();
    // Test-only data instantiates a row; production starts with an empty source model.
    std::unique_ptr<QObject> sourceModel;
    if (panelName == QStringLiteral("SourcesPanel")) {
        QQmlComponent component(panel.engine());
        component.setData(QStringLiteral("import QtQml.Models\nListModel { ListElement { "
                          "sourceId: 'image'; sourceName: 'A long source name for layout verification'; "
                          "sourceVisible: %1; sourceLocked: %2; sourceCaptureState: ''; sourceCaptureError: '' } }")
                          .arg(alternateSourceState ? "false" : "true",
                               alternateSourceState ? "true" : "false").toUtf8(), QUrl());
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
        const bool isolateIcon = panelName == QStringLiteral("SourcesPanel")
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

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    if (QGuiApplication::platformName() != "offscreen" && QGuiApplication::platformName() != "minimal")
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    application.setQuitOnLastWindowClosed(false);
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_mainwindow.moc"

void MainWindowTest::sourceFiltersDialogEditsAndRetainsState_data()
{
    QTest::addColumn<QString>("themeId");
    QTest::newRow("dark") << QString("dark");
    QTest::newRow("light") << QString("light");
}

void MainWindowTest::sourceFiltersDialogEditsAndRetainsState()
{
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme);
    window.show();
    auto *model = window.findChild<SourceListModel *>();
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    QVERIFY(model && panel && panel->rootObject());
    std::function<QQuickItem *(QQuickItem *, const QString &)> findItem;
    findItem = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems())
            if (auto *found = findItem(child, name)) return found;
        return nullptr;
    };
    auto *button = findItem(panel->rootObject(), "sourceFiltersButton");
    QVERIFY(button && !button->isEnabled());
    QVERIFY(model->addSource({"camera", "Camera", CameraConfig{"unavailable-test-camera"}}));
    QVERIFY(button->isEnabled());
    QVERIFY(model->addSource({"image", "Image", ImageConfig{}}));
    QVERIFY(button->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    auto *dialog = window.findChild<QDialog *>();
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_VERIFY(view && view->rootObject());
    auto *root = view->rootObject();
    QCOMPARE(root->objectName(), QString("sourceFiltersDialog"));
    QCOMPARE(root->property("sourceId").toString(), QString("image"));
    QCOMPARE(root->property("color").value<QColor>(), m_theme->color("background.panel"));
    const auto click = [&](const QString &name) {
        auto *item = findItem(root, name);
        QVERIFY2(item, qPrintable(name));
        QVERIFY(item->isEnabled());
        QVERIFY(QMetaObject::invokeMethod(item, "clicked"));
    };
    QVERIFY(!findItem(root, "brightnessSlider")->isEnabled());
    click("addFilterButton");
    QCOMPARE(model->sourceFilters("image").size(), 1);
    const QString first = root->property("selectedId").toString();
    auto *slider = findItem(root, "brightnessSlider");
    QVERIFY(slider && slider->isEnabled());
    slider->setProperty("value", .25);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QCOMPARE(model->sourceFilters("image")[0].toMap()["brightness"].toFloat(), .25f);
    slider->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Right);
    QTRY_VERIFY(qAbs(model->sourceFilters("image")[0].toMap()["brightness"].toFloat() - .26f) < .001f);
    auto *input = findItem(root, "contrastInput");
    QVERIFY(input);
    input->setProperty("value", 150);
    QVERIFY(QMetaObject::invokeMethod(input, "valueModified"));
    QCOMPARE(model->sourceFilters("image")[0].toMap()["contrast"].toFloat(), 1.5f);
    // Exercise an actual keyboard edit through the decimal SpinBox editor.
    auto *editor = input->property("contentItem").value<QQuickItem *>();
    QVERIFY(editor);
    editor->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(view, "1.25");
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_COMPARE(model->sourceFilters("image")[0].toMap()["contrast"].toFloat(), 1.25f);
    click("addFilterButton");
    const QString second = root->property("selectedId").toString();
    QVERIFY(second != first);
    click("upFilterButton");
    QCOMPARE(model->sourceFilters("image")[0].toMap()["id"].toString(), second);
    click("downFilterButton");
    QCOMPARE(model->sourceFilters("image")[1].toMap()["id"].toString(), second);
    QVERIFY(!view->grabFramebuffer().isNull());
    click("toggleFilter_" + second);
    QVERIFY(!model->sourceFilters("image")[1].toMap()["enabled"].toBool());
    click("removeFilterButton");
    QCOMPARE(root->property("selectedId").toString(), first);
    QVERIFY(model->selectSource("camera"));
    QCOMPARE(root->property("sourceId").toString(), QString("image"));
    QVERIFY(model->sourceFilters("camera").isEmpty());
    QVERIFY(model->setSourceLocked("image", true));
    click("resetFilterButton");
    QCOMPARE(model->sourceFilters("image")[0].toMap()["brightness"].toFloat(), 0.f);
    QCOMPARE(model->sourceFilters("image")[0].toMap()["contrast"].toFloat(), 1.f);
    QVERIFY(model->setFilterParameters("image", first, .4, .8, 1.2));
    QVERIFY(view->grabFramebuffer().save(QCoreApplication::applicationDirPath()
        + QString("/filter-dialog-%1.png").arg(themeId)));
    QPointer<QDialog> previous(dialog);
    click("closeFiltersButton");
    QTRY_VERIFY(previous.isNull());
    QVERIFY(model->selectSource("image"));
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    dialog = window.findChild<QDialog *>();
    QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_VERIFY(view && view->rootObject());
    root = view->rootObject();
    QCOMPARE(root->property("selectedId").toString(), first);
    QVERIFY(qAbs(findItem(root, "brightnessSlider")->property("value").toDouble() - .4) < .001);
    previous = dialog;
    QVERIFY(model->removeSource("image"));
    QTRY_VERIFY(previous.isNull());
}

void MainWindowTest::displayCaptureDialogs_data()
{
    QTest::addColumn<QString>("themeId");
    QTest::newRow("dark") << QString("dark");
    QTest::newRow("light") << QString("light");
}

void MainWindowTest::displayCaptureDialogs()
{
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme);
    window.show();
    auto *model = window.findChild<SourceListModel *>();
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    QVERIFY(model && panel);
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourceAddDialogRequested"));
    QPointer<QDialog> add = window.findChild<QDialog *>();
    QVERIFY(add);
    auto *view = add->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_VERIFY(view && view->rootObject());
    auto *root = view->rootObject();
    auto *settings = root->findChild<QQuickItem *>("displaySettings");
    QVERIFY(settings && settings->isVisible());
    settings->setProperty("displayId", "missing-test-display");
    settings->setProperty("captureCursor", false);
    root->findChild<QQuickItem *>("sourceNameInput")->setProperty("text", "Screen");
    QVERIFY(view->grabFramebuffer().save(QCoreApplication::applicationDirPath() + QString("/display-add-%1.png").arg(themeId)));
    auto *confirm = root->findChild<QQuickItem *>("confirmSourceAddButton");
    QVERIFY(confirm && confirm->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked"));
    QTRY_VERIFY(add.isNull());
    QCOMPARE(model->rowCount(), 1);
    const QString id = model->currentSourceId();
    QCOMPARE(model->displayProperties(id).value("displayId").toString(), QString("missing-test-display"));
    QVERIFY(!model->displayProperties(id).value("captureCursor").toBool());
    QTRY_COMPARE(model->displayProperties(id).value("state").toString(), QString("error"));
    QVERIFY(model->canEditCurrentDisplay() && model->canFilterCurrentSource());
    const QString filter = model->addColorCorrection(id);
    const SourceTransform transform{{800, 400}, {600, 300}, 25};
    QVERIFY(model->setSourceTransform(id, transform));
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourcePropertiesDialogRequested", Q_ARG(QString, id)));
    QPointer<QDialog> properties = window.findChild<QDialog *>();
    QVERIFY(properties);
    view = properties->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_VERIFY(view && view->rootObject());
    root = view->rootObject();
    QCOMPARE(root->objectName(), QString("displayPropertiesDialog"));
    settings = root->findChild<QQuickItem *>("displaySettings");
    QCOMPARE(settings->property("displayId").toString(), QString("missing-test-display"));
    QVERIFY(!settings->property("captureCursor").toBool());
    auto *target = settings->findChild<QQuickItem *>("displayTargetInput");
    QVERIFY(target && target->property("currentIndex").toInt() >= 0);
    QVERIFY(target->property("currentText").toString().contains(QString::fromUtf8("断开")));
    auto *error = root->findChild<QQuickItem *>("captureErrorLabel");
    QVERIFY(error && !error->property("text").toString().isEmpty());
    settings->setProperty("displayId", "another-missing-test-display");
    settings->setProperty("captureCursor", true);
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QQuickItem *>("applyDisplayPropertiesButton"), "clicked"));
    QCOMPARE(model->displayProperties(id).value("displayId").toString(), QString("another-missing-test-display"));
    QVERIFY(model->displayProperties(id).value("captureCursor").toBool());
    QTRY_COMPARE(model->displayProperties(id).value("state").toString(), QString("error"));
    QCOMPARE(model->index(0, 0).data(SourceListModel::TransformRole).value<SourceTransform>(), transform);
    QCOMPARE(model->sourceFilters(id)[0].toMap()["id"].toString(), filter);
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QQuickItem *>("retryDisplayButton"), "clicked"));
    QTRY_COMPARE(model->displayProperties(id).value("state").toString(), QString("error"));
    for (const auto &theme : {themeId == "dark" ? QString("light") : QString("dark"), themeId}) {
        QVERIFY(m_theme->setTheme(theme));
        QCOMPARE(root->property("color").value<QColor>(), m_theme->color("background.panel"));
        QCOMPARE(error->property("color").value<QColor>(), m_theme->color("status.error"));
        QCOMPARE(settings->property("displayId").toString(), QString("another-missing-test-display"));
    }
    QVERIFY(view->grabFramebuffer().save(QCoreApplication::applicationDirPath() + QString("/display-properties-%1.png").arg(themeId)));
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourceFiltersDialogRequested", Q_ARG(QString, id)));
    QTRY_COMPARE(window.findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly).size(), 2);
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourcePropertiesDialogRequested", Q_ARG(QString, id)));
    QCOMPARE(window.findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly).size(), 2);
    QVERIFY(model->removeSource(id));
    QTRY_VERIFY(properties.isNull());
    QTRY_VERIFY(window.findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly).isEmpty());
}

void MainWindowTest::cameraPropertiesDialogs_data() { displayCaptureDialogs_data(); }
void MainWindowTest::cameraPropertiesDialogs()
{
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme); window.show();
    auto *model = window.findChild<SourceListModel *>();
    auto *controller = window.findChild<CameraCaptureController *>();
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    QVERIFY(model && controller && panel && panel->rootObject());
    const QVariantMap mode{{"width", 1280}, {"height", 720}, {"numerator", 30000}, {"denominator", 1001}};
    const QString id = controller->addCamera("Test Camera", "unavailable-ui-camera", mode);
    QVERIFY(!id.isEmpty()); QVERIFY(model->setSourceVisible(id, false));
    const auto original = model->cameraProperties(id);
    const auto open = [&] {
        QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourcePropertiesDialogRequested", Q_ARG(QString, id)));
    };
    open();
    auto *dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget");
    QTRY_VERIFY(view && view->rootObject());
    auto *root = view->rootObject(); QCOMPARE(root->objectName(), QString("cameraPropertiesDialog"));
    auto *settings = root->findChild<QQuickItem *>("cameraSettings"); QVERIFY(settings);
    QCOMPARE(settings->property("deviceId").toString(), QString("unavailable-ui-camera"));
    QTRY_VERIFY(!settings->property("loadingModes").toBool());
    QCOMPARE(settings->property("selectedMode").toMap().value("denominator").toInt(), 1001);
    const QVariantMap changed{{"width", 640}, {"height", 480}, {"numerator", 30}, {"denominator", 1}};
    QVERIFY(view->grabFramebuffer().save(QCoreApplication::applicationDirPath() + "/camera-properties-" + themeId + ".png"));
    settings->setProperty("selectedMode", changed);
    QCOMPARE(model->cameraProperties(id), original);
    QPointer<QDialog> first(dialog);
    auto *cancel = root->findChild<QQuickItem *>("cancelCameraPropertiesButton"); QVERIFY(cancel);
    QVERIFY(QMetaObject::invokeMethod(cancel, "clicked")); QTRY_VERIFY(first.isNull());
    QCOMPARE(model->cameraProperties(id), original);
    open(); dialog = window.findChild<QDialog *>(); QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view->rootObject()); root = view->rootObject();
    settings = root->findChild<QQuickItem *>("cameraSettings"); QVERIFY(settings);
    QTRY_VERIFY(!settings->property("loadingModes").toBool()); settings->setProperty("selectedMode", changed);
    auto *confirm = root->findChild<QQuickItem *>("confirmCameraPropertiesButton"); QVERIFY(confirm && confirm->isEnabled());
    QPointer<QDialog> second(dialog);
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked")); QTRY_VERIFY(second.isNull());
    QCOMPARE(model->cameraProperties(id).value("width").toInt(), 640);
    QCOMPARE(model->cameraProperties(id).value("state").toString(), QString("paused"));
    open(); QPointer<QDialog> third(window.findChild<QDialog *>()); QVERIFY(third);
    open(); QCOMPARE(window.findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly).size(), 1);
    QVERIFY(model->removeSource(id)); QTRY_VERIFY(third.isNull());
}

void MainWindowTest::cameraHardwarePreview()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_CAMERA_HARDWARE")) QSKIP("Requires explicit camera hardware validation");
    MainWindow window(*m_theme); window.show();
    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    QVERIFY2(preview, "Requires an interactive desktop OpenGL preview");
    QTRY_VERIFY_WITH_TIMEOUT(preview->isValid(), 10000);
    auto *model = window.findChild<SourceListModel *>();
    auto *controller = window.findChild<CameraCaptureController *>();
    controller->refreshDevices();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->refreshing(), 10000);
    QVERIFY2(controller->catalogError().isEmpty(), qPrintable(controller->catalogError()));
    const auto devices = controller->devices(); QVERIFY(!devices.isEmpty());
    QSignalSpy failures(preview, &PreviewWidget::sourceError);
    const QString id = controller->addCamera("Camera", devices[0].toMap().value("id").toString(), {});
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model->cameraProperties(id).value("state").toString(), QString("capturing"), 15000);
    const auto filter = model->addColorCorrection(id);
    QVERIFY(model->setFilterParameters(id, filter, 0, 1, 0));
    QTest::qWait(500);
    QVERIFY(preview->lastError().isEmpty()); QVERIFY(failures.isEmpty());
    const auto frame = grabPreview(*preview); QVERIFY(!frame.isNull());
    const QColor pixel = frame.pixelColor(frame.width() / 2, frame.height() / 2);
    QVERIFY(qAbs(pixel.red() - pixel.green()) <= 3 && qAbs(pixel.green() - pixel.blue()) <= 3);
    // Keep the mouse pressed while real camera frames arrive between edits.
    // Check the entire model -> controller -> preview -> model path.
    const SourceTransform start{{960, 540}, {600, 300}, 0};
    for (int operation = 0; operation < 3; ++operation) {
        QVERIFY(model->setSourceTransform(id, start));
        QTest::qWait(100);
        const auto display = SourceGeometry::canvasToWidget(preview->size());
        const auto center = display.map(start.center);
        const QPointF from = operation == 0 ? center : operation == 1
            ? display.map(SourceGeometry::sourceToCanvas(start).map(QPointF(600, 300)))
            : display.map(SourceGeometry::sourceToCanvas(start).map(QPointF(300, 0))) - QPointF(0, 24);
        QPointF to;
        if (operation == 2) {
            QTransform rotation; rotation.rotate(30);
            to = center + rotation.map(from - center);
        } else {
            const QPointF delta = operation == 0 ? QPointF(120, 60) : QPointF(60, 30);
            to = from + display.map(delta) - display.map(QPointF());
        }
        const auto mouse = [&](QEvent::Type type, const QPointF &position, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, preview->mapToGlobal(position), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(preview, &event);
        };
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        QTest::qWait(150);
        mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QTest::qWait(150);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);
        const auto actual = model->index(0, 0).data(SourceListModel::TransformRole).value<SourceTransform>();
        if (operation == 0) QVERIFY(QLineF(actual.center, QPointF(1080, 600)).length() < 0.001);
        else if (operation == 1) {
            QVERIFY(qAbs(actual.size.width() - 660) < 0.001);
            QVERIFY(qAbs(actual.size.height() - 330) < 0.001);
        } else QVERIFY(qAbs(actual.rotation - 30) < 0.001);
    }
    window.resize(1100, 720); QTest::qWait(100);
    QVERIFY(preview->lastError().isEmpty());
    window.showMinimized();
    QCoreApplication::processEvents();
    const auto minimizedSequence = preview->compositor()->latestFrame()->sequence;
    const auto minimizedRevision = preview->compositor()->latestFrame()->revision;
    QTRY_VERIFY(preview->compositor()->latestFrame()->sequence > minimizedSequence);
    QTRY_VERIFY(preview->compositor()->latestFrame()->revision > minimizedRevision);
    QVERIFY(preview->compositor()->latestFrame()->sources.contains(id));
    QCOMPARE(controller->activeWorkerCount(), 1);
    QVERIFY(!preview->compositor()->captureImage().isNull());
    window.showNormal();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(model->setSourceVisible(id, false));
    QTRY_COMPARE_WITH_TIMEOUT(controller->activeWorkerCount(), 0, 10000);
    QVERIFY(model->removeSource(id));
}

void MainWindowTest::addsCameraThroughDialog()
{
    MainWindow window(*m_theme); window.show();
    auto *model = window.findChild<SourceListModel *>();
    auto *panel = window.findChild<QQuickWidget *>("sourcesQuickWidget");
    QVERIFY(model && panel && panel->rootObject());
    QVERIFY(QMetaObject::invokeMethod(panel->rootObject(), "sourceAddDialogRequested"));
    QPointer<QDialog> dialog(window.findChild<QDialog *>()); QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>("dialogQuickWidget"); QTRY_VERIFY(view && view->rootObject());
    auto *root = view->rootObject();
    auto *type = root->findChild<QQuickItem *>("sourceTypeInput"); QVERIFY(type);
    type->setProperty("currentIndex", 2);
    auto *settings = root->findChild<QQuickItem *>("cameraSettings"); QVERIFY(settings && settings->isVisible());
    settings->setProperty("deviceId", "missing-add-dialog-camera");
    QTRY_VERIFY(!settings->property("loadingModes").toBool());
    auto *name = root->findChild<QQuickItem *>("sourceNameInput"); QVERIFY(name);
    name->setProperty("text", "Camera fixture");
    auto *confirm = root->findChild<QQuickItem *>("confirmSourceAddButton"); QVERIFY(confirm && confirm->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(confirm, "clicked")); QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->index(0, 0).data(SourceListModel::TypeRole).toString(), QString("camera"));
    const QString id = model->currentSourceId();
    QCOMPARE(model->cameraProperties(id).value("deviceId").toString(), QString("missing-add-dialog-camera"));
    QVERIFY(model->canFilterCurrentSource()); QVERIFY(model->canEditCurrentSource());
    QTRY_COMPARE_WITH_TIMEOUT(model->cameraProperties(id).value("state").toString(), QString("error"), 10000);
    QVERIFY(model->removeSource(id));
}
