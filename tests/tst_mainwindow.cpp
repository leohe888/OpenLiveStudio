#include "mainwindow.h"
#include "thememanager.h"
#include "studiostate.h"
#include "scenesnapshotbuilder.h"
#include "compositionsession.h"
#include "previewwidget.h"
#include <QSurfaceFormat>
#include <QStackedWidget>
#include <QOpenGLContext>
#include <QApplication>

#include <QAction>
#include <QAbstractButton>
#include <QDockWidget>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
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
#include <QTemporaryDir>
#include <QToolBar>
#include <QWheelEvent>
#include <QtTest/QTest>
#include <memory>

class MainWindowTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_settings;
    std::unique_ptr<ThemeManager> m_theme;

private slots:
    void init();
    void canShowAndClose();
    void hasWindowShell();
    void themeRestoresAndUpdatesOuterWidgets();
    void dialogsShareThemeAndReleaseBeforeIt();
    void basicDialogThemes_data();
    void basicDialogThemes();
    void sceneAndSourceThemes_data();
    void sceneAndSourceThemes();
    void mixerAndControlThemes_data();
    void mixerAndControlThemes();
    void panelsShareStateAcrossReloadsAndReleaseItWithWindow();
    void panelsFollowSceneAndSourceSelection();
    void imageScenesSwitchIndependently_data();
    void imageScenesSwitchIndependently();
    void sourcePanelCanMoveAndToggle_data();
    void sourcePanelCanMoveAndToggle();
    void listScrollBarsHandleOverflow_data();
    void listScrollBarsHandleOverflow();
    void qmlDialogReceivesInitialPropertiesAndCloses();
    void qmlDialogShowsLoadErrors_data();
    void qmlDialogShowsLoadErrors();
    void aboutActionShowsProjectInfoAndReusesDialog();
    void sourcePanelCanBrowseAndAdd_data();
    void sourcePanelCanBrowseAndAdd();
    void sourceAddDialogHandlesInput_data();
    void sourceAddDialogHandlesInput();
    void sceneNameDialogValidatesAndSaves_data();
    void sceneNameDialogValidatesAndSaves();
    void sceneNameDialogCancelsWithoutChanges_data();
    void sceneNameDialogCancelsWithoutChanges();
    void scenePanelCanAddAndRenameAfterReload();
    void sourcePanelCanRemove_data();
    void sourcePanelCanRemove();
    void sourceRemovalDialogHandlesConfirmation_data();
    void sourceRemovalDialogHandlesConfirmation();
    void sceneRemovalDialogHandlesConfirmation_data();
    void sceneRemovalDialogHandlesConfirmation();
    void scenePanelCanRemoveAndMove_data();
    void scenePanelCanRemoveAndMove();
    void keepsOutputStatusVisibleWhenResized();
    void loadsScenePanelWithEmptyState();
    void loadsSourcePanelWithEmptyState();
    void loadsMixerPanelWithUnavailableChannels();
    void loadsControlsPanelWithVerticalButtons();
    void embedsScenePanelInLeftDock();
    void embedsSourcePanelBelowSceneDock();
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

void MainWindowTest::panelsShareStateAcrossReloadsAndReleaseItWithWindow()
{
    auto window = std::make_unique<MainWindow>(*m_theme);
    window->show();
    QPointer<StudioState> state = window->findChild<StudioState *>();
    QVERIFY(state);
    QCOMPARE(window->findChildren<StudioState *>().size(), 1);
    QCOMPARE(state->parent(), window.get());
    const QString sceneId = state->currentSceneId();
    QVERIFY(!sceneId.isEmpty());
    QPointer<QAbstractItemModel> scenes = state->sceneModel();
    QPointer<QAbstractItemModel> sources = state->sourceModel();
    QList<QPointer<QQuickItem>> roots;
    QStringList destructionOrder;
    connect(state, &QObject::destroyed, this, [&] { destructionOrder.append("state"); });

    for (const auto *panelId : {"scenes", "sources"}) {
        auto *view = window->findChild<QQuickWidget *>(QLatin1String(panelId) + QStringLiteral("QuickWidget"));
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty(QStringLiteral("studioState")).value<QObject *>(), state.data());
        // Exercise the QML context and invokable API, rather than just a C++ pointer.
        QQmlExpression expression(view->rootContext(), view->rootObject(),
            QStringLiteral("studioState.selectScene(studioState.currentSceneId)"));
        QCOMPARE(expression.evaluate().toBool(), true);
        QVERIFY2(!expression.hasError(), qPrintable(expression.error().toString()));
        QPointer<QQuickItem> oldRoot = view->rootObject();
        const QUrl source = view->source();
        view->setSource(QUrl(QStringLiteral("qrc:/missing-state-panel.qml")));
        QTRY_COMPARE(view->status(), QQuickWidget::Error);
        QTRY_VERIFY(oldRoot.isNull());
        view->setSource(source);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        QCOMPARE(view->rootContext()->contextProperty(QStringLiteral("studioState")).value<QObject *>(), state.data());
        QQmlExpression reloaded(view->rootContext(), view->rootObject(),
            QStringLiteral("studioState.currentSceneId"));
        QCOMPARE(reloaded.evaluate().toString(), sceneId);
        QVERIFY2(!reloaded.hasError(), qPrintable(reloaded.error().toString()));
        QCOMPARE(state->sceneModel(), scenes.data());
        QCOMPARE(state->sourceModel(), sources.data());
        roots.append(view->rootObject());
        connect(view->rootObject(), &QObject::destroyed, this,
            [&] { destructionOrder.append("panel"); });
    }

    window.reset();
    QVERIFY(state.isNull());
    QVERIFY(scenes.isNull());
    QVERIFY(sources.isNull());
    for (const auto &root : roots)
        QVERIFY(root.isNull());
    QCOMPARE(destructionOrder, QStringList({"panel", "panel", "state"}));
}

void MainWindowTest::panelsFollowSceneAndSourceSelection()
{
    StudioState state(QList<StudioState::SceneData>{
        {"first", "Scene", {{"a", "Image"}}},
        {"second", "Scene", {{"b", "Image"}}},
        {"empty", "Empty", {}}});
    QQuickWidget scenes;
    scenes.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    QQuickWidget sources;
    sources.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    for (auto *view : {&scenes, &sources}) {
        view->setResizeMode(QQuickWidget::SizeRootObjectToView);
        view->resize(238, 260);
        view->rootContext()->setContextProperty(QStringLiteral("studioState"), &state);
    }
    scenes.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ScenesPanel.qml")));
    sources.setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
    for (auto *view : {&scenes, &sources}) {
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        view->show();
    }
    const auto row = [](QQuickWidget &view, const QString &listName, const QString &rowName) -> QQuickItem * {
        auto *list = view.rootObject()->findChild<QQuickItem *>(listName);
        if (!list)
            return nullptr;
        auto *content = list->property("contentItem").value<QQuickItem *>();
        if (!content)
            return nullptr;
        for (auto *item : content->childItems()) {
            if (item->objectName() == rowName)
                return item;
        }
        return nullptr;
    };
    const auto click = [](QQuickWidget &view, QQuickItem *item) {
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(40, item->height() / 2)).toPoint());
    };
    QTRY_VERIFY(row(scenes, "sceneList", "sceneRow_first"));
    QTRY_VERIFY(row(scenes, "sceneList", "sceneRow_second"));
    QTRY_VERIFY(row(scenes, "sceneList", "sceneRow_empty"));
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_a"));
    QVERIFY(row(scenes, "sceneList", "sceneRow_first")->property("selected").toBool());
    QVERIFY(!row(scenes, "sceneList", "sceneRow_second")->property("selected").toBool());
    QVERIFY(!row(sources, "sourceList", "sourceRow_a")->property("selected").toBool());
    click(sources, row(sources, "sourceList", "sourceRow_a"));
    QTRY_COMPARE(state.currentSourceId(), QString("a"));
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_a")->property("selected").toBool());
    click(scenes, row(scenes, "sceneList", "sceneRow_second"));
    QTRY_COMPARE(state.currentSceneId(), QString("second"));
    QVERIFY(state.currentSourceId().isEmpty());
    QTRY_VERIFY(row(scenes, "sceneList", "sceneRow_second")->property("selected").toBool());
    QVERIFY(!row(scenes, "sceneList", "sceneRow_first")->property("selected").toBool());
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_b"));
    QVERIFY(!row(sources, "sourceList", "sourceRow_b")->property("selected").toBool());
    QVERIFY(!row(sources, "sourceList", "sourceRow_a"));
    QVERIFY(!state.selectSource("a"));
    click(sources, row(sources, "sourceList", "sourceRow_b"));
    QTRY_COMPARE(state.currentSourceId(), QString("b"));
    // Recreated QML must derive its highlight from C++, not a previous delegate.
    const QUrl source = sources.source();
    sources.setSource(QUrl());
    sources.setSource(source);
    QTRY_COMPARE(sources.status(), QQuickWidget::Ready);
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_b"));
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_b")->property("selected").toBool());
    QVERIFY(state.selectScene("empty"));
    auto *empty = sources.rootObject()->findChild<QQuickItem *>(QStringLiteral("emptySourcesLabel"));
    QVERIFY(empty);
    QTRY_VERIFY(empty->isVisible());
    QTRY_VERIFY(row(scenes, "sceneList", "sceneRow_empty")->property("selected").toBool());
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(state.selectScene("first"));
    QTRY_VERIFY(!empty->isVisible());
    QTRY_VERIFY(row(sources, "sourceList", "sourceRow_a"));
    QVERIFY(!row(sources, "sourceList", "sourceRow_a")->property("selected").toBool());
}

void MainWindowTest::imageScenesSwitchIndependently_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("recovered") << true;
}

void MainWindowTest::imageScenesSwitchIndependently()
{
    QFETCH(bool, reload);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("same-image.png");
    QImage image(20, 12, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    const QString first = state->currentSceneId();
    const QString second = state->addScene("Second");
    const QString empty = state->addScene("Empty");
    QVERIFY(!second.isEmpty());
    QVERIFY(!empty.isEmpty());
    const QString a = state->addImageSource(first, "Logo", path);
    const QString aBackground = state->addImageSource(first, "Background", path);
    const QString b = state->addImageSource(second, "Logo", path);
    QVERIFY(!a.isEmpty());
    QVERIFY(!aBackground.isEmpty());
    QVERIFY(!b.isEmpty());
    QVERIFY(a != b);
    QVERIFY(state->selectScene(first));
    auto *scenes = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    auto *sources = window.findChild<QQuickWidget *>(QStringLiteral("sourcesQuickWidget"));
    QVERIFY(scenes);
    QVERIFY(sources);
    for (auto *panel : {scenes, sources}) {
        QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
        if (reload) {
            const QUrl url = panel->source();
            panel->setSource(QUrl(QStringLiteral("qrc:/missing-scene-switch-panel.qml")));
            QTRY_COMPARE(panel->status(), QQuickWidget::Error);
            panel->setSource(url);
            QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
        }
    }
    const auto find = [](QQuickWidget *panel, const QString &name) -> QQuickItem * {
        const auto search = [&name](auto &&self, QQuickItem *parent) -> QQuickItem * {
            if (parent->objectName() == name) return parent;
            for (auto *child : parent->childItems()) {
                if (auto *found = self(self, child)) return found;
            }
            return nullptr;
        };
        return search(search, panel->rootObject());
    };
    const auto click = [](QQuickWidget *panel, QQuickItem *item, bool label = false) {
        QVERIFY2(item, "The clickable QML item is not ready");
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(label ? 40 : item->width() / 2, item->height() / 2)).toPoint());
    };
    QTRY_VERIFY(find(scenes, "sceneRow_" + first));
    QTRY_VERIFY(find(scenes, "sceneRow_" + second));
    QTRY_VERIFY(find(scenes, "sceneRow_" + empty));
    QTRY_VERIFY(find(sources, "sourceRow_" + a));
    click(sources, find(sources, "sourceRow_" + a), true);
    QTRY_COMPARE(state->currentSourceId(), a);
    auto *up = find(sources, "moveSourceUpButton");
    QVERIFY(up);
    QTRY_VERIFY(up->isEnabled());
    click(sources, up);
    auto *row = find(sources, "sourceRow_" + a);
    QVERIFY(row);
    auto *visibility = row->findChild<QQuickItem *>(QStringLiteral("sourceVisibilityButton"));
    auto *lock = row->findChild<QQuickItem *>(QStringLiteral("sourceLockButton"));
    QVERIFY(visibility);
    QVERIFY(lock);
    click(sources, visibility);
    click(sources, lock);
    QTRY_VERIFY(!row->property("sourceVisible").toBool());
    QTRY_VERIFY(row->property("sourceLocked").toBool());
    QTRY_COMPARE(state->sourceModel()->index(0, 0).data(StudioState::IdRole).toString(), a);
    QSignalSpy changed(state, &StudioState::currentSceneIdChanged);
    QSignalSpy reset(state->sourceModel(), &QAbstractItemModel::modelReset);
    click(scenes, find(scenes, "sceneRow_" + first), true);
    QCOMPARE(state->currentSourceId(), a); // Reclicking the current scene preserves selection.
    QCOMPARE(changed.count(), 0);
    QCOMPARE(reset.count(), 0);
    for (int iteration = 0; iteration < 4; ++iteration) {
        click(scenes, find(scenes, "sceneRow_" + second), true);
        QCOMPARE(state->currentSceneId(), second); // C++ state changes without an animation timer.
        QVERIFY(state->currentSourceId().isEmpty());
        QTRY_VERIFY(find(sources, "sourceRow_" + b));
        auto *other = find(sources, "sourceRow_" + b);
        QVERIFY(other->property("sourceVisible").toBool());
        QVERIFY(!other->property("sourceLocked").toBool());
        click(sources, other, true);
        QTRY_COMPARE(state->currentSourceId(), b);
        click(scenes, find(scenes, "sceneRow_" + first), true);
        QCOMPARE(state->currentSceneId(), first);
        QVERIFY(state->currentSourceId().isEmpty());
        QTRY_VERIFY(find(sources, "sourceRow_" + a));
        auto *restored = find(sources, "sourceRow_" + a);
        QVERIFY(!restored->property("sourceVisible").toBool());
        QVERIFY(restored->property("sourceLocked").toBool());
        QVERIFY(!restored->property("selected").toBool());
        QCOMPARE(state->sourceModel()->index(0, 0).data(StudioState::IdRole).toString(), a);
    }
    QCOMPARE(changed.count(), 8);
    QCOMPARE(reset.count(), 8);
    click(scenes, find(scenes, "sceneRow_" + empty), true);
    QCOMPARE(state->currentSceneId(), empty);
    QCOMPARE(state->sourceModel()->rowCount(), 0);
    QTRY_VERIFY(find(sources, "emptySourcesLabel")->isVisible());
    QVERIFY(!find(sources, "removeSourceButton")->isEnabled());
    QVERIFY(!find(sources, "moveSourceUpButton")->isEnabled());
    QVERIFY(!find(sources, "moveSourceDownButton")->isEnabled());
    click(scenes, find(scenes, "sceneRow_" + first), true);
    QTRY_VERIFY(find(sources, "sourceRow_" + a));
    const QUrl url = sources->source();
    sources->setSource(QUrl());
    sources->setSource(url);
    QTRY_COMPARE(sources->status(), QQuickWidget::Ready);
    QTRY_VERIFY(find(sources, "sourceRow_" + a));
    QVERIFY(!find(sources, "sourceRow_" + a)->property("sourceVisible").toBool());
    QVERIFY(find(sources, "sourceRow_" + a)->property("sourceLocked").toBool());
    QVERIFY(state->currentSourceId().isEmpty());
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
    StudioState state(QList<StudioState::SceneData>{
        {"first", "First", {{"a", "Foreground"}, {"b", "Background"}, {"c", "Logo"}}},
        {"second", "Second", {{"b", "Other scene image"}}}, {"empty", "Empty", {}}});
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(280, 220);
    panel.rootContext()->setContextProperty(QStringLiteral("studioState"), &state);
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
    QVERIFY(!state.sourceModel()->index(1, 0).data(StudioState::VisibleRole).toBool());
    click(lock);
    QTRY_VERIFY(lock->property("checked").toBool());
    QTRY_COMPARE(iconSource(lock), QUrl("qrc:/assets/icons/lock.svg"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(state.sourceModel()->index(1, 0).data(StudioState::LockedRole).toBool());
    // Model updates must continue to drive presentation after user clicks.
    QVERIFY(state.setSourceVisible("first", "b", true));
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
    QTRY_COMPARE(state.sourceModel()->index(0, 0).data(StudioState::IdRole).toString(), QString("b"));
    QTRY_VERIFY(!up->isEnabled());
    QCOMPARE(state.currentSourceId(), QString("b"));
    QTRY_VERIFY(find("sourceRow_b")->y() < find("sourceRow_a")->y());
    click(down);
    click(down);
    QTRY_COMPARE(state.sourceModel()->index(2, 0).data(StudioState::IdRole).toString(), QString("b"));
    QTRY_VERIFY(!down->isEnabled());
    click(down);
    QCOMPARE(state.currentSourceId(), QString("b"));
    QVERIFY(state.selectScene("second"));
    QTRY_VERIFY(!up->isEnabled());
    QTRY_VERIFY(!down->isEnabled());
    QVERIFY(state.selectScene("first"));
    QVERIFY(state.selectSource("b"));
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
    QVERIFY(state.selectScene("empty"));
    QTRY_VERIFY(find("emptySourcesLabel")->isVisible());
    QVERIFY(!find("moveSourceUpButton")->isEnabled());
    QVERIFY(!find("moveSourceDownButton")->isEnabled());
}

void MainWindowTest::listScrollBarsHandleOverflow_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<QString>("prefix");
    QTest::newRow("scenes") << QString("ScenesPanel") << QString("scene");
    QTest::newRow("sources") << QString("SourcesPanel") << QString("source");
}

void MainWindowTest::listScrollBarsHandleOverflow()
{
    QFETCH(QString, panelName);
    QFETCH(QString, prefix);
    QList<StudioState::SourceData> sources;
    QList<StudioState::SceneData> scenes;
    for (int i = 0; i < 24; ++i)
        sources.append({QString::number(i), QStringLiteral("Source %1").arg(i)});
    for (int i = 0; i < 24; ++i)
        scenes.append({QString::number(i), QStringLiteral("Scene %1").arg(i), sources});
    StudioState state(scenes);
    StudioState shortState(QList<StudioState::SceneData>{{"only", "Only", {{"one", "One"}}}});
    QVERIFY(state.selectSource("0"));
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
    panel.setResizeMode(QQuickWidget::SizeRootObjectToView);
    panel.resize(238, 150);
    panel.rootContext()->setContextProperty(QStringLiteral("studioState"), &state);
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
    QCOMPARE(state.currentSceneId(), QString("0"));
    QCOMPARE(state.currentSourceId(), QString("0"));
    panel.resize(238, 1000);
    QTRY_VERIFY(!bar->isVisible());
    panel.resize(238, 150);
    QTRY_VERIFY(bar->isVisible());
    // A short list needs no scrollbar even in the smaller panel.
    panel.rootContext()->setContextProperty(QStringLiteral("studioState"), &shortState);
    QTRY_COMPARE(list->property("count").toInt(), 1);
    QTRY_VERIFY(!bar->isVisible());
}

void MainWindowTest::hasWindowShell()
{
    MainWindow window(*m_theme);
    window.show();

    QVERIFY(window.centralWidget());
    QVERIFY(window.centralWidget()->isVisible());
    QVERIFY(window.menuBar()->isVisible());
    QCOMPARE(window.menuBar()->actions().size(), 2);
    QVERIFY(window.findChildren<QToolBar *>().isEmpty());
    QVERIFY(window.statusBar()->isVisible());
    auto *unavailable = window.findChild<QLabel *>(QStringLiteral("previewUnavailable"));
    QVERIFY(unavailable);
    QVERIFY(!unavailable->text().isEmpty());
    QVERIFY(!window.findChild<PreviewWidget *>());
    QVERIFY(!window.findChild<CompositionSession *>());
    QVERIFY(!window.findChild<SceneSnapshotBuilder *>());
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
    QCOMPARE(views.size(), 4);
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
    auto *failure = window.findChild<QLabel *>("previewUnavailable");
    QVERIFY(failure);
    QTRY_COMPARE(failure->palette().color(QPalette::WindowText), m_theme->color("status.error"));
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

void MainWindowTest::sceneAndSourceThemes_data()
{
    QTest::addColumn<QString>("panelName");
    QTest::addColumn<QString>("prefix");
    QTest::addColumn<QString>("themeId");
    for (const auto &theme : {QString("dark"), QString("light")}) {
        QTest::newRow(qPrintable("scenes-" + theme)) << QString("ScenesPanel") << QString("scene") << theme;
        QTest::newRow(qPrintable("sources-" + theme)) << QString("SourcesPanel") << QString("source") << theme;
    }
}

void MainWindowTest::sceneAndSourceThemes()
{
    QFETCH(QString, panelName);
    QFETCH(QString, prefix);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    QList<StudioState::SourceData> sources;
    QList<StudioState::SceneData> scenes;
    for (int i = 0; i < 24; ++i)
        sources.append({QString::number(i), QString("Source %1").arg(i), {}, {}, {}, i != 0, i == 0});
    for (int i = 0; i < 24; ++i)
        scenes.append({QString::number(i), QString("Scene %1").arg(i), sources});
    StudioState state(scenes);
    QVERIFY(state.selectSource("0"));
    QQuickWidget panel;
    panel.rootContext()->setContextProperty("uiTheme", m_theme.get());
    panel.rootContext()->setContextProperty("studioState", &state);
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
        const auto labelText = prefix == "scene" ? QString("Scene 0") : QString("Source 0");
        bool foundLabel = false;
        for (auto *label : row->findChildren<QQuickItem *>()) {
            if (label->property("text").toString() == labelText) {
                QCOMPARE(label->property("color").value<QColor>(), m_theme->color("text.primary"));
                foundLabel = true;
            }
        }
        QVERIFY(foundLabel);
        auto *add = find(prefix == "scene" ? "addSceneButton" : "addSourceButton");
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
            QVERIFY(filters && !filters->isEnabled());
            QCOMPARE(QQmlProperty::read(filters, "icon.color").value<QColor>(), m_theme->color("icon.disabled"));
            QVERIFY(find("sourceVisibilityButton")->property("checked").toBool() == false);
            QVERIFY(find("sourceLockButton")->property("checked").toBool());
        }
        QCOMPARE(state.currentSceneId(), "0");
        QCOMPARE(state.currentSourceId(), "0");
    }
    panel.setSource(QUrl());
    panel.setSource(url);
    QTRY_COMPARE(panel.status(), QQuickWidget::Ready);
    QTRY_VERIFY(find(prefix + "Row_0"));
    QCOMPARE(find(prefix + "Row_0")->property("color").value<QColor>(), m_theme->color("background.selected"));
    QCOMPARE(state.currentSceneId(), "0");
    QCOMPARE(state.currentSourceId(), "0");
    if (prefix == "source") {
        const auto empty = state.addScene("Empty");
        QVERIFY(state.selectScene(empty));
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
        for (const auto &name : {QString("AboutDialog"), QString("SceneNameDialog"), QString("SceneRemovalDialog"), QString("SourceAddDialog"), QString("SourceRemovalDialog")})
            QTest::newRow(qPrintable(name + "-" + theme)) << name << theme;
}

void MainWindowTest::basicDialogThemes()
{
    QFETCH(QString, dialogName);
    QFETCH(QString, themeId);
    QVERIFY(m_theme->setTheme(themeId));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    QVariantMap properties;
    if (dialogName == "AboutDialog") {
        properties = {{"productName", "OpenLive Studio"}, {"versionText", "Development"}, {"qtVersion", qVersion()}};
    } else {
        properties = {{"state", QVariant::fromValue(state)}, {"sceneId", state->currentSceneId()}};
        if (dialogName.startsWith("Source")) {
            if (dialogName == "SourceAddDialog") properties.insert("initialName", "Theme sample");
            else {
                properties.insert("sourceId", "fixed-source-id");
                properties.insert("sourceName", "Theme sample");
            }
        } else properties.insert(dialogName == "SceneNameDialog" ? "initialName" : "sceneName", "Theme sample");
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
        auto *input = root->findChild<QQuickItem *>(dialogName == "SourceAddDialog" ? "sourceNameInput" : "sceneNameInput");
        auto *file = root->findChild<QQuickItem *>("sourceFileInput");
        if (file) root->setProperty("filePath", "C:/Images/unsaved image.png");
        const auto sceneId = root->property("sceneId");
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
            QCOMPARE(root->property("sceneId"), sceneId);
            QCOMPARE(root->property("sourceId"), sourceId);
            if (file) {
                QCOMPARE(file->property("text").toString(), "C:/Images/unsaved image.png");
                QCOMPARE(file->property("placeholderTextColor").value<QColor>(), m_theme->color("text.secondary"));
            }
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
                const auto token = name.endsWith("Error") ? "status.error"
                    : (name == "aboutVersion" || name == "aboutQtVersion" || name == "sourceRemovalHint" ? "text.secondary" : "text.primary");
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
            QCOMPARE(file->property("text").toString(), "C:/Images/unsaved image.png");
            QCOMPARE(root->property("errorText").toString(), "Validation sample");
            type->setEnabled(false);
            QCOMPARE(indicator->property("arrowColor").value<QColor>(), m_theme->color("icon.disabled"));
            type->setEnabled(true);
        }
        auto *button = root->findChild<QQuickItem *>(dialogName == "AboutDialog" ? "closeAboutButton"
            : (dialogName == "SceneNameDialog" ? "cancelSceneNameButton"
                : (dialogName == "SourceAddDialog" ? "cancelSourceAddButton"
                    : (dialogName == "SourceRemovalDialog" ? "cancelSourceRemovalButton" : "cancelSceneRemovalButton"))));
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

void MainWindowTest::sourcePanelCanBrowseAndAdd_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("reloaded") << true;
}

void MainWindowTest::sourcePanelCanBrowseAndAdd()
{
    QFETCH(bool, reload);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QString::fromUtf8("图像 #100% space.png"));
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    auto *panel = window.findChild<QQuickWidget *>(QStringLiteral("sourcesQuickWidget"));
    QVERIFY(state && panel);
    if (reload) {
        panel->setSource(QUrl(QStringLiteral("qrc:/missing-source-panel.qml")));
        QTRY_COMPARE(panel->status(), QQuickWidget::Error);
        panel->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
    }
    QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
    auto *actions = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceActions"));
    QVERIFY(actions);
    QQuickItem *add = nullptr;
    for (auto *item : actions->childItems()) {
        if (item->objectName() == QStringLiteral("addSourceButton"))
            add = item;
    }
    QVERIFY(add && add->isEnabled());
    QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
        add->mapToScene(add->boundingRect().center()).toPoint());
    QTRY_COMPARE(window.findChildren<QDialog *>(QStringLiteral("qmlDialog")).size(), 1);
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QCOMPARE(dialog->windowTitle(), QString::fromUtf8("添加来源"));
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QPointer<QFileDialog> picker = dialog->findChild<QFileDialog *>(QStringLiteral("imageFileDialog"));
    auto *name = root->findChild<QQuickItem *>(QStringLiteral("sourceNameInput"));
    QVERIFY(name);
    QCOMPARE(name->property("text").toString(), QString::fromUtf8("图像"));
    auto *file = root->findChild<QQuickItem *>(QStringLiteral("sourceFileInput"));
    auto *browse = root->findChild<QQuickItem *>(QStringLiteral("browseSourceFileButton"));
    QVERIFY(picker && file && browse);
    QCOMPARE(picker->parentWidget(), dialog.data());
    picker->setOption(QFileDialog::DontUseNativeDialog);
    picker->setDirectory(dir.path());
    file->forceActiveFocus();
    QTest::keyClicks(view, "previous.png");
    const auto openPicker = [&] {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            browse->mapToScene(browse->boundingRect().center()).toPoint());
    };
    openPicker();
    QTRY_VERIFY(picker->isVisible());
    picker->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(picker, "reject"));
    QTRY_VERIFY(!picker->isVisible());
    QCOMPARE(file->property("text").toString(), QString("previous.png"));
    QCOMPARE(state->sourceModel()->rowCount(), 0);
    openPicker();
    QTRY_VERIFY(picker->isVisible());
    picker->selectFile(path);
    QVERIFY(QMetaObject::invokeMethod(picker, "accept"));
    QTRY_VERIFY(!picker->isVisible());
    QTRY_COMPARE(file->property("text").toString(), path);
    QCOMPARE(root->property("filePath").toString(), path);
    QCOMPARE(state->sourceModel()->rowCount(), 0); // Picking is not creation.
    QVERIFY(QMetaObject::invokeMethod(root, "submit"));
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(picker.isNull());
    QCOMPARE(state->sourceModel()->rowCount(), 1);
    QCOMPARE(state->sourceModel()->index(0, 0).data(StudioState::FilePathRole).toString(), path);
    QCOMPARE(state->currentSourceId(), state->sourceModel()->index(0, 0).data(StudioState::IdRole).toString());
    QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
        add->mapToScene(add->boundingRect().center()).toPoint());
    QTRY_COMPARE(window.findChildren<QDialog *>(QStringLiteral("qmlDialog")).size(), 1);
    dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    picker = dialog->findChild<QFileDialog *>(QStringLiteral("imageFileDialog"));
    name = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceNameInput"));
    QVERIFY(name);
    QCOMPARE(name->property("text").toString(), QString::fromUtf8("图像 2"));
    QVERIFY(picker);
    picker->setOption(QFileDialog::DontUseNativeDialog);
    picker->setDirectory(dir.path());
    QVERIFY(QMetaObject::invokeMethod(picker, "open"));
    QTRY_VERIFY(picker->isVisible());
    dialog->close();
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(picker.isNull());
    QCOMPARE(state->sourceModel()->rowCount(), 1);
}

void MainWindowTest::sourceAddDialogHandlesInput_data()
{
    QTest::addColumn<QString>("method");
    for (const auto *method : {"enter", "click", "cancel", "escape", "close", "stale", "switched"})
        QTest::newRow(method) << QString::fromLatin1(method);
}

void MainWindowTest::sourceAddDialogHandlesInput()
{
    QFETCH(QString, method);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("image.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    const QString sceneId = state->currentSceneId();
    const QString existingId = state->addImageSource(sceneId, "Existing", path);
    QVERIFY(!existingId.isEmpty());
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml")),
        QStringLiteral("Add source"), {{"state", QVariant::fromValue(state)}, {"sceneId", sceneId}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    const auto find = [&](const char *name) { return root->findChild<QQuickItem *>(QString::fromLatin1(name)); };
    auto *name = find("sourceNameInput");
    auto *file = find("sourceFileInput");
    auto *error = find("sourceAddError");
    auto *type = find("sourceTypeInput");
    auto *browse = find("browseSourceFileButton");
    auto *confirm = find("confirmSourceAddButton");
    auto *cancel = find("cancelSourceAddButton");
    QVERIFY(name && file && error && type && browse && confirm && cancel);
    QCOMPARE(type->property("count").toInt(), 1);
    QVERIFY(browse->isEnabled());
    QCOMPARE(name->property("text").toString(), QString::fromUtf8("图像"));
    QTRY_VERIFY(name->hasActiveFocus());
    QCOMPARE(name->property("selectedText").toString(), name->property("text").toString());
    const auto input = [&](QQuickItem *field, const QString &text) {
        field->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_Backspace);
        QTest::keyClicks(view, text);
    };
    const auto click = [&](QQuickItem *button) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
    };
    input(file, path);
    if (method == "enter" || method == "click") {
        for (const auto &invalid : {QString("  "), QString(" existing ")}) {
            input(name, invalid);
            QTest::keyClick(view, Qt::Key_Return);
            QTRY_VERIFY(!error->property("text").toString().isEmpty());
            QCOMPARE(state->sourceModel()->rowCount(), 1);
            QCOMPARE(state->currentSourceId(), existingId);
            QVERIFY(dialog->isVisible());
        }
        input(name, "Logo");
        input(file, dir.filePath("missing.png"));
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(!error->property("text").toString().isEmpty());
        QCOMPARE(name->property("text").toString(), QString("Logo"));
        QCOMPARE(state->sourceModel()->rowCount(), 1);
        input(file, path);
        QTRY_VERIFY(error->property("text").toString().isEmpty());
    }
    input(name, "  Logo  ");
    if (method == "cancel" || method == "escape" || method == "close") {
        // Tab follows the path, Browse, Confirm and Cancel controls.
        QTest::keyClick(view, Qt::Key_Tab);
        QTRY_VERIFY(file->hasActiveFocus());
        if (method == "cancel") {
            QTest::keyClick(view, Qt::Key_Tab);
            QTRY_VERIFY(browse->hasActiveFocus());
            QTest::keyClick(view, Qt::Key_Tab);
            QTRY_VERIFY(confirm->hasActiveFocus());
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
        QCOMPARE(state->sourceModel()->rowCount(), 1);
        QCOMPARE(state->currentSourceId(), existingId);
        return;
    }
    QString otherId;
    if (method == "stale" || method == "switched") {
        otherId = state->addScene("Other");
        QVERIFY(!otherId.isEmpty());
        if (method == "stale")
            QVERIFY(state->removeScene(sceneId));
    }
    if (method == "click")
        click(confirm);
    else
        QTest::keyClick(view, Qt::Key_Return);
    if (method == "stale") {
        QTRY_VERIFY(!error->property("text").toString().isEmpty());
        QVERIFY(dialog->isVisible());
        QCOMPARE(state->sourceModel()->rowCount(), 0);
        QCOMPARE(state->currentSceneId(), otherId);
        QCOMPARE(name->property("text").toString(), QString("  Logo  "));
        dialog->close();
        QTRY_VERIFY(dialog.isNull());
        return;
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    if (method == "switched") {
        QCOMPARE(state->currentSceneId(), otherId);
        QCOMPARE(state->sourceModel()->rowCount(), 0);
        QVERIFY(state->selectScene(sceneId));
    }
    QCOMPARE(state->sourceModel()->rowCount(), 2);
    const auto added = state->sourceModel()->index(0, 0);
    QCOMPARE(added.data(StudioState::NameRole).toString(), QString("Logo"));
    QCOMPARE(added.data(StudioState::FilePathRole).toString(), path);
    QCOMPARE(added.data(StudioState::OriginalSizeRole).toSize(), image.size());
    if (method != "switched")
        QCOMPARE(state->currentSourceId(), added.data(StudioState::IdRole).toString());
}

void MainWindowTest::sceneNameDialogValidatesAndSaves_data()
{
    QTest::addColumn<bool>("rename");
    QTest::newRow("add-enter") << false;
    QTest::newRow("rename-click") << true;
}

void MainWindowTest::sceneNameDialogValidatesAndSaves()
{
    QFETCH(bool, rename);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    const QString originalId = state->currentSceneId();
    QVERIFY(!state->addScene("Existing").isEmpty());
    QVERIFY(state->selectScene(originalId));
    const QString initial = rename ? state->sceneModel()->index(0, 0).data(StudioState::NameRole).toString()
                                   : state->suggestedSceneName();
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SceneNameDialog.qml")),
        QStringLiteral("Scene name"), {{"state", QVariant::fromValue(state)},
        {"initialName", initial}, {"sceneId", rename ? originalId : QString()}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    auto *input = root->findChild<QQuickItem *>(QStringLiteral("sceneNameInput"));
    auto *error = root->findChild<QQuickItem *>(QStringLiteral("sceneNameError"));
    QVERIFY(input);
    QVERIFY(error);
    QCOMPARE(input->property("text").toString(), initial);
    QTRY_VERIFY(input->hasActiveFocus());
    QCOMPARE(input->property("selectedText").toString(), initial);
    const auto type = [&](const QString &text) {
        input->forceActiveFocus();
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_Backspace);
        QTest::keyClicks(view, text);
    };
    for (const auto &invalid : {QString("   "), QString(" existing ")}) {
        type(invalid);
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(!error->property("text").toString().isEmpty());
        QVERIFY(dialog->isVisible());
        QCOMPARE(state->sceneModel()->rowCount(), 2);
        QCOMPARE(state->currentSceneId(), originalId);
    }
    type("  Saved name  ");
    QTRY_VERIFY(error->property("text").toString().isEmpty());
    if (rename) {
        auto *button = root->findChild<QQuickItem *>(QStringLiteral("confirmSceneNameButton"));
        QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
    } else {
        QTest::keyClick(view, Qt::Key_Return);
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    QCOMPARE(state->sceneModel()->rowCount(), rename ? 2 : 3);
    QCOMPARE(state->sceneModel()->index(rename ? 0 : 2, 0).data(StudioState::NameRole).toString(), QString("Saved name"));
    if (rename)
        QCOMPARE(state->currentSceneId(), originalId);
    else
        QVERIFY(state->currentSceneId() != originalId);
}

void MainWindowTest::sceneNameDialogCancelsWithoutChanges_data()
{
    QTest::addColumn<QString>("method");
    QTest::newRow("cancel") << QString("cancel");
    QTest::newRow("escape") << QString("escape");
    QTest::newRow("window-close") << QString("close");
}

void MainWindowTest::sceneNameDialogCancelsWithoutChanges()
{
    QFETCH(QString, method);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    const QString originalId = state->currentSceneId();
    const QString originalName = state->sceneModel()->index(0, 0).data(StudioState::NameRole).toString();
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SceneNameDialog.qml")),
        QStringLiteral("Scene name"), {{"state", QVariant::fromValue(state)},
        {"initialName", originalName}, {"sceneId", originalId}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *input = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneNameInput"));
    QVERIFY(input);
    QTRY_VERIFY(input->hasActiveFocus());
    QTest::keyClicks(view, "Unsaved");
    if (method == "cancel") {
        auto *button = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("cancelSceneNameButton"));
        QVERIFY(button);
        // Keyboard focus moves from the input to Confirm and then Cancel.
        QTest::keyClick(view, Qt::Key_Tab);
        QTest::keyClick(view, Qt::Key_Tab);
        QTRY_VERIFY(button->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Space);
    } else if (method == "escape") {
        QTest::keyClick(view, Qt::Key_Escape);
    } else {
        dialog->close();
    }
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(state->sceneModel()->rowCount(), 1);
    QCOMPARE(state->currentSceneId(), originalId);
    QCOMPARE(state->sceneModel()->index(0, 0).data(StudioState::NameRole).toString(), originalName);
}

void MainWindowTest::scenePanelCanAddAndRenameAfterReload()
{
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    auto *panel = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    QVERIFY(state);
    QVERIFY(panel);
    const QString originalId = state->currentSceneId();
    const QUrl source = panel->source();
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            panel->setSource(QUrl(QStringLiteral("qrc:/missing-scene-panel.qml")));
            QTRY_COMPARE(panel->status(), QQuickWidget::Error);
            panel->setSource(source);
        }
        QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
        auto *actions = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneActions"));
        QVERIFY(actions);
        QQuickItem *add = nullptr;
        for (auto *button : actions->childItems()) {
            if (!button->objectName().endsWith(QStringLiteral("Button")))
                continue;
            if (button->objectName() == QStringLiteral("addSceneButton")) {
                add = button;
                QVERIFY(button->isEnabled());
            }
        }
        QVERIFY(add);
        const QString suggestion = state->suggestedSceneName();
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
            add->mapToScene(add->boundingRect().center()).toPoint());
        QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
        QVERIFY(dialog);
        QCOMPARE(window.findChildren<QDialog *>().size(), 1);
        QCOMPARE(dialog->windowTitle(), QString::fromUtf8("添加场景"));
        auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        auto *input = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneNameInput"));
        QVERIFY(input);
        QCOMPARE(input->property("text").toString(), suggestion);
        QTRY_VERIFY(input->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(dialog.isNull());
        const QString id = state->currentSceneId();
        QVERIFY(id != originalId);
        QCOMPARE(state->sceneModel()->rowCount(), pass + 2);
        QCOMPARE(state->sourceModel()->rowCount(), 0);
        auto *list = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneList"));
        QVERIFY(list);
        const auto findRow = [&]() -> QQuickItem * {
            auto *content = list->property("contentItem").value<QQuickItem *>();
            if (content) {
                for (auto *item : content->childItems()) {
                    if (item->objectName() == QStringLiteral("sceneRow_") + id)
                        return item;
                }
            }
            return nullptr;
        };
        QTRY_VERIFY(findRow());
        QVERIFY(findRow()->property("selected").toBool());
        const QPoint position = findRow()->mapToScene(findRow()->boundingRect().center()).toPoint();
        // The QWindow helper sends both complete clicks with double-click timing.
        QTest::mouseDClick(panel->quickWindow(), Qt::LeftButton, Qt::NoModifier, position);
        QTRY_VERIFY(window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
        dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
        QCOMPARE(window.findChildren<QDialog *>().size(), 1);
        QCOMPARE(dialog->windowTitle(), QString::fromUtf8("重命名场景"));
        view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
        QVERIFY(view);
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        input = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneNameInput"));
        QVERIFY(input);
        QCOMPARE(input->property("text").toString(), suggestion);
        QTRY_VERIFY(input->hasActiveFocus());
        const QString renamed = QStringLiteral("Renamed %1").arg(pass);
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(view, renamed);
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(state->currentSceneId(), id);
        QTRY_COMPARE(findRow()->property("sceneName").toString(), renamed);
        QCOMPARE(state->sceneModel()->rowCount(), pass + 2);
    }
}

void MainWindowTest::sourcePanelCanRemove_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("reloaded") << true;
}

void MainWindowTest::sourcePanelCanRemove()
{
    QFETCH(bool, reload);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("image.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    auto *panel = window.findChild<QQuickWidget *>(QStringLiteral("sourcesQuickWidget"));
    QVERIFY(state && panel);
    QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
    const auto removeButton = [&]() -> QQuickItem * {
        auto *actions = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceActions"));
        if (actions) {
            for (auto *item : actions->childItems()) {
                if (item->objectName() == QStringLiteral("removeSourceButton")) return item;
            }
        }
        return nullptr;
    };
    const auto click = [](QQuickWidget *view, QQuickItem *item) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(QPointF(15, item->height() / 2)).toPoint());
    };
    QVERIFY(removeButton());
    QVERIFY(!removeButton()->isEnabled());
    click(panel, removeButton());
    QVERIFY(!window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
    const QString sceneId = state->currentSceneId();
    const QString first = state->addImageSource(sceneId, "Alpha", path);
    const QString second = state->addImageSource(sceneId, "Beta", path);
    QVERIFY(!first.isEmpty() && !second.isEmpty());
    if (reload) {
        const QUrl url = panel->source();
        panel->setSource(QUrl(QStringLiteral("qrc:/missing-source-removal-panel.qml")));
        QTRY_COMPARE(panel->status(), QQuickWidget::Error);
        panel->setSource(url);
        QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
    }
    QTRY_VERIFY(removeButton() && removeButton()->isEnabled());
    QVERIFY(state->selectSource({}));
    QTRY_VERIFY(!removeButton()->isEnabled());
    auto *list = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sourceList"));
    QVERIFY(list);
    const auto firstRow = [&]() -> QQuickItem * {
        auto *content = list->property("contentItem").value<QQuickItem *>();
        if (content) {
            for (auto *item : content->childItems()) {
                if (item->objectName() == "sourceRow_" + first) return item;
            }
        }
        return nullptr;
    };
    QTRY_VERIFY(firstRow());
    click(panel, firstRow());
    QTRY_COMPARE(state->currentSourceId(), first);
    QTRY_VERIFY(removeButton()->isEnabled());
    for (const auto &action : {QString("cancel"), QString("confirm")}) {
        click(panel, removeButton());
        QTRY_COMPARE(window.findChildren<QDialog *>(QStringLiteral("qmlDialog")).size(), 1);
        QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
        QCOMPARE(dialog->windowTitle(), QString::fromUtf8("移除来源"));
        auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
        QTRY_COMPARE(view->status(), QQuickWidget::Ready);
        auto *root = view->rootObject();
        QCOMPARE(root->property("sceneId").toString(), sceneId);
        QCOMPARE(root->property("sourceId").toString(), first);
        QCOMPARE(root->property("sourceName").toString(), QString("Alpha"));
        auto *button = root->findChild<QQuickItem *>(action == "cancel"
            ? QStringLiteral("cancelSourceRemovalButton") : QStringLiteral("confirmSourceRemovalButton"));
        QVERIFY(button);
        click(view, button);
        QTRY_VERIFY(dialog.isNull());
        QCOMPARE(state->sourceModel()->rowCount(), action == "cancel" ? 2 : 1);
        QCOMPARE(state->currentSourceId(), action == "cancel" ? first : QString());
    }
    QTRY_VERIFY(!removeButton()->isEnabled());
    QVERIFY(state->selectSource(second));
    QTRY_VERIFY(removeButton()->isEnabled());
    click(panel, removeButton());
    QTRY_VERIFY(window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *confirm = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("confirmSourceRemovalButton"));
    QVERIFY(confirm);
    click(view, confirm);
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(state->sourceModel()->rowCount(), 0);
    QVERIFY(state->currentSourceId().isEmpty());
    auto *empty = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("emptySourcesLabel"));
    QVERIFY(empty);
    QTRY_VERIFY(empty->isVisible());
    QTRY_VERIFY(!removeButton()->isEnabled());
    click(panel, removeButton());
    QVERIFY(!window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
    QVERIFY(QFile::exists(path));
}

void MainWindowTest::sourceRemovalDialogHandlesConfirmation_data()
{
    QTest::addColumn<QString>("action");
    for (const auto *name : {"confirm", "switched", "last", "cancel", "escape", "close", "stale-source", "stale-scene"})
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
    QList<StudioState::SourceData> sources{{"shared", name, "image", path, image.size()}};
    if (action != "last") sources.append(StudioState::SourceData{"remaining", "Remaining"});
    StudioState state(QList<StudioState::SceneData>{
        {"target", "Target", sources}, {"other", "Other", {{"shared", "Other image"}}}});
    QVERIFY(state.selectSource("shared"));
    MainWindow window(*m_theme);
    window.show();
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")),
        QStringLiteral("Remove source"), {{"state", QVariant::fromValue(&state)},
        {"sceneId", "target"}, {"sourceId", "shared"}, {"sourceName", name}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    auto *message = root->findChild<QQuickItem *>(QStringLiteral("sourceRemovalMessage"));
    auto *confirm = root->findChild<QQuickItem *>(QStringLiteral("confirmSourceRemovalButton"));
    auto *cancel = root->findChild<QQuickItem *>(QStringLiteral("cancelSourceRemovalButton"));
    auto *error = root->findChild<QQuickItem *>(QStringLiteral("sourceRemovalError"));
    QVERIFY(message && confirm && cancel && error);
    QVERIFY(message->property("text").toString().contains(name));
    QCOMPARE(message->property("textFormat").toInt(), 0); // Text.PlainText
    QTRY_VERIFY(cancel->hasActiveFocus());
    QCOMPARE(state.sourceModel()->rowCount(), sources.size());
    if (action == "switched") {
        QVERIFY(state.selectScene("other"));
        QVERIFY(state.selectSource("shared"));
    } else if (action == "stale-source") {
        QVERIFY(state.removeSource("target", "shared"));
    } else if (action == "stale-scene") {
        QVERIFY(state.removeScene("target"));
    }
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
        if (action.startsWith("stale-")) {
            QTRY_VERIFY(!error->property("text").toString().isEmpty());
            QVERIFY(dialog->isVisible());
            click(cancel);
        }
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    if (action == "switched") {
        QCOMPARE(state.currentSceneId(), QString("other"));
        QCOMPARE(state.currentSourceId(), QString("shared"));
        QCOMPARE(state.sourceModel()->rowCount(), 1);
    }
    const bool removed = action == "confirm" || action == "switched" || action == "last" || action == "stale-source";
    if (action == "stale-scene") {
        QVERIFY(!state.selectScene("target"));
    } else {
        QVERIFY(state.selectScene("target"));
        QCOMPARE(state.sourceModel()->rowCount(), sources.size() - (removed ? 1 : 0));
        if (removed) {
            QVERIFY(!state.selectSource("shared"));
            QVERIFY(state.currentSourceId().isEmpty());
        } else {
            QCOMPARE(state.currentSourceId(), QString("shared"));
        }
    }
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
    QVERIFY(state.selectScene("other"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QVERIFY(state.selectSource("shared"));
}

void MainWindowTest::sceneRemovalDialogHandlesConfirmation_data()
{
    QTest::addColumn<QString>("action");
    for (const auto *name : {"confirm", "cancel", "escape", "close", "stale", "last"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void MainWindowTest::sceneRemovalDialogHandlesConfirmation()
{
    QFETCH(QString, action);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    QVERIFY(state);
    const QString targetId = state->currentSceneId();
    const QString name = QStringLiteral("<b>Scene & name</b>");
    QVERIFY(state->renameScene(targetId, name));
    const QString otherId = action == "last" ? QString() : state->addScene("Other");
    QVERIFY(state->selectScene(targetId));
    const int originalCount = state->sceneModel()->rowCount();
    window.showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SceneRemovalDialog.qml")),
        QStringLiteral("Remove scene"), {{"state", QVariant::fromValue(state)},
        {"sceneId", targetId}, {"sceneName", name}});
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QPointer<QQuickItem> root = view->rootObject();
    QVERIFY(root);
    auto *message = root->findChild<QQuickItem *>(QStringLiteral("sceneRemovalMessage"));
    auto *confirm = root->findChild<QQuickItem *>(QStringLiteral("confirmSceneRemovalButton"));
    auto *cancel = root->findChild<QQuickItem *>(QStringLiteral("cancelSceneRemovalButton"));
    auto *error = root->findChild<QQuickItem *>(QStringLiteral("sceneRemovalError"));
    QVERIFY(message);
    QVERIFY(confirm);
    QVERIFY(cancel);
    QVERIFY(error);
    QVERIFY(message->property("text").toString().contains(name));
    QCOMPARE(message->property("textFormat").toInt(), 0); // Text.PlainText
    QTRY_VERIFY(cancel->hasActiveFocus());
    QCOMPARE(state->sceneModel()->rowCount(), originalCount);
    if (action == "confirm") {
        // A changed row or current selection must not change the captured target.
        QVERIFY(state->moveScene(targetId, 1));
        QVERIFY(state->selectScene(otherId));
    } else if (action == "stale") {
        QVERIFY(state->removeScene(targetId));
    }
    const auto click = [&](QQuickItem *button) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
    };
    if (action == "cancel") {
        click(cancel);
    } else if (action == "escape") {
        QTest::keyClick(view, Qt::Key_Escape);
    } else if (action == "close") {
        dialog->close();
    } else {
        click(confirm);
        if (action == "stale" || action == "last") {
            QVERIFY(dialog->isVisible());
            QTRY_VERIFY(!error->property("text").toString().isEmpty());
            QCOMPARE(state->sceneModel()->rowCount(), 1);
            const QString remainingId = action == "last" ? targetId : otherId;
            QCOMPARE(state->currentSceneId(), remainingId);
            click(cancel);
        }
    }
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(root.isNull());
    if (action == "confirm" || action == "stale") {
        QCOMPARE(state->sceneModel()->rowCount(), 1);
        QCOMPARE(state->currentSceneId(), otherId);
        QVERIFY(!state->selectScene(targetId));
    } else {
        QCOMPARE(state->sceneModel()->rowCount(), originalCount);
        QCOMPARE(state->currentSceneId(), targetId);
    }
}

void MainWindowTest::scenePanelCanRemoveAndMove_data()
{
    QTest::addColumn<bool>("reload");
    QTest::newRow("initial") << false;
    QTest::newRow("reloaded") << true;
}

void MainWindowTest::scenePanelCanRemoveAndMove()
{
    QFETCH(bool, reload);
    MainWindow window(*m_theme);
    window.show();
    auto *state = window.findChild<StudioState *>();
    auto *panel = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    QVERIFY(state);
    QVERIFY(panel);
    if (reload) {
        const QUrl source = panel->source();
        panel->setSource(QUrl(QStringLiteral("qrc:/missing-scene-actions.qml")));
        QTRY_COMPARE(panel->status(), QQuickWidget::Error);
        panel->setSource(source);
    }
    QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
    auto *actions = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneActions"));
    QVERIFY(actions);
    const auto button = [&](const QString &name) -> QQuickItem * {
        for (auto *item : actions->childItems()) {
            if (item->objectName() == name)
                return item;
        }
        return nullptr;
    };
    auto *remove = button("removeSceneButton");
    auto *up = button("moveSceneUpButton");
    auto *down = button("moveSceneDownButton");
    QVERIFY(remove);
    QVERIFY(up);
    QVERIFY(down);
    QVERIFY(!remove->isEnabled());
    QVERIFY(!up->isEnabled());
    QVERIFY(!down->isEnabled());
    const auto click = [&](QQuickWidget *view, QQuickItem *item) {
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            item->mapToScene(item->boundingRect().center()).toPoint());
    };
    click(panel, remove);
    QVERIFY(!window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
    const QString firstId = state->currentSceneId();
    for (int i = 0; i < 20; ++i)
        QVERIFY(!state->addScene(QStringLiteral("Scene %1").arg(i)).isEmpty());
    const QString lastId = state->currentSceneId();
    QTRY_VERIFY(remove->isEnabled());
    QTRY_VERIFY(up->isEnabled());
    QVERIFY(!down->isEnabled());
    auto *list = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneList"));
    auto *bar = panel->rootObject()->findChild<QQuickItem *>(QStringLiteral("sceneScrollBar"));
    QVERIFY(list);
    QVERIFY(bar);
    QTRY_VERIFY(bar->isVisible());
    QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtEnd"));
    QTRY_VERIFY(list->property("atYEnd").toBool());
    click(panel, up);
    QCOMPARE(state->sceneModel()->index(19, 0).data(StudioState::IdRole).toString(), lastId);
    QCOMPARE(state->currentSceneId(), lastId);
    QTRY_VERIFY(down->isEnabled());
    click(panel, down);
    QCOMPARE(state->sceneModel()->index(20, 0).data(StudioState::IdRole).toString(), lastId);
    QTRY_VERIFY(!down->isEnabled());
    QVERIFY(state->selectScene(firstId));
    QTRY_VERIFY(!up->isEnabled());
    QTRY_VERIFY(down->isEnabled());
    click(panel, down);
    QCOMPARE(state->sceneModel()->index(1, 0).data(StudioState::IdRole).toString(), firstId);
    QTRY_VERIFY(up->isEnabled());
    click(panel, up);
    QCOMPARE(state->sceneModel()->index(0, 0).data(StudioState::IdRole).toString(), firstId);
    const QString nextId = state->sceneModel()->index(1, 0).data(StudioState::IdRole).toString();
    click(panel, remove);
    QPointer<QDialog> dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    QCOMPARE(window.findChildren<QDialog *>().size(), 1);
    QCOMPARE(dialog->windowTitle(), QString::fromUtf8("删除场景"));
    auto *view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QCOMPARE(view->rootObject()->property("sceneId").toString(), firstId);
    auto *cancel = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("cancelSceneRemovalButton"));
    QVERIFY(cancel);
    QTRY_VERIFY(cancel->hasActiveFocus());
    QTest::keyClick(view, Qt::Key_Escape);
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(state->sceneModel()->rowCount(), 21);
    QCOMPARE(state->currentSceneId(), firstId);
    click(panel, remove);
    dialog = window.findChild<QDialog *>(QStringLiteral("qmlDialog"));
    QVERIFY(dialog);
    view = dialog->findChild<QQuickWidget *>(QStringLiteral("dialogQuickWidget"));
    QVERIFY(view);
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    auto *confirm = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("confirmSceneRemovalButton"));
    QVERIFY(confirm);
    click(view, confirm);
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(state->sceneModel()->rowCount(), 20);
    QCOMPARE(state->currentSceneId(), nextId);
    while (state->canRemoveCurrentScene())
        QVERIFY(state->removeScene(state->currentSceneId()));
    QTRY_VERIFY(!remove->isEnabled());
    QTRY_VERIFY(!up->isEnabled());
    QTRY_VERIFY(!down->isEnabled());
    click(panel, remove);
    QCOMPARE(state->sceneModel()->rowCount(), 1);
    QVERIFY(!window.findChild<QDialog *>(QStringLiteral("qmlDialog")));
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

void MainWindowTest::loadsScenePanelWithEmptyState()
{
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
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
    QQuickWidget panel;
    panel.rootContext()->setContextProperty(QStringLiteral("uiTheme"), m_theme.get());
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
    QList<QRectF> originalChannels;
    for (auto *channel : channelItems)
        originalChannels.append(channel->mapRectToScene(channel->boundingRect()));
    for (const auto &size : {QSize(160, 140), QSize(238, 178), QSize(400, 500)}) {
        panel.resize(size);
        QTRY_COMPARE(root->size(), QSizeF(size));
        QVERIFY(scroll->property("contentWidth").toReal() >= scroll->width());
        QVERIFY(!channelItems[0]->mapRectToScene(channelItems[0]->boundingRect()).intersects(
            channelItems[1]->mapRectToScene(channelItems[1]->boundingRect())));
        for (int i = 0; i < channelItems.size(); ++i) {
            const QRectF rect = channelItems[i]->mapRectToScene(channelItems[i]->boundingRect());
            QCOMPARE(rect.left(), originalChannels[i].left());
            QCOMPARE(rect.width(), originalChannels[i].width());
        }
        if (size == QSize(238, 178))
            QVERIFY(scroll->property("contentHeight").toReal() <= scroll->height());
    }
    // Short panels retain a scrollable channel layout instead of clipping controls.
    panel.resize(160, 140);
    QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
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

void MainWindowTest::embedsScenePanelInLeftDock()
{
    MainWindow window(*m_theme);
    window.show();
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *view = window.findChild<QQuickWidget *>(QStringLiteral("scenesQuickWidget"));
    QVERIFY(dock);
    QVERIFY(view);
    QCOMPARE(window.dockWidgetArea(dock), Qt::LeftDockWidgetArea);
    QVERIFY(dock->isAncestorOf(view));
    QTRY_COMPARE(view->status(), QQuickWidget::Ready);
    QVERIFY(view->rootObject());
    auto *list = view->rootObject()->findChild<QObject *>(QStringLiteral("sceneList"));
    QVERIFY(list);
    QTRY_COMPARE(list->property("count").toInt(), 1);
    auto *empty = view->rootObject()->findChild<QQuickItem *>(QStringLiteral("emptyScenesLabel"));
    QVERIFY(empty);
    QVERIFY(!empty->isVisible());
    QTRY_VERIFY(dock->isVisible());
    QVERIFY(view->isVisible());
}

void MainWindowTest::embedsSourcePanelBelowSceneDock()
{
    MainWindow window(*m_theme);
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
            QCOMPARE(button->isEnabled(), button->objectName() == QStringLiteral("addSourceButton"));
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
    QCOMPARE(window.findChildren<QDockWidget *>().size(), 4);
    QVERIFY(!window.findChild<QDockWidget *>(QStringLiteral("transitionsDock")));
    QVERIFY(!window.findChild<QQuickWidget *>(QStringLiteral("transitionsQuickWidget")));
    QVERIFY(!window.findChild<QAction *>(QStringLiteral("transitionsDockViewAction")));
    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    QVERIFY(viewMenu);
    QCOMPARE(viewMenu->actions().size(), 6); // Four docks, separator and reset.
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
            QCOMPARE(button->isEnabled(), button->objectName() == QStringLiteral("addSourceButton"));
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
    QCOMPARE(docks.size(), 4);
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
    const QList<Qt::DockWidgetArea> areas{Qt::LeftDockWidgetArea, Qt::LeftDockWidgetArea,
        Qt::BottomDockWidgetArea, Qt::BottomDockWidgetArea};
    for (const auto *id : {"scenes", "sources", "mixer", "controls"}) {
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
            docks[3]->close();
        } else if (pass == 1) {
            for (auto *dock : docks) {
                dock->setFloating(true);
                dock->close();
            }
        }
        resetAction->trigger();
        QTRY_COMPARE(window.size(), windowSize);
        QCOMPARE(window.findChildren<QDockWidget *>().size(), 4);
        QCOMPARE(window.findChildren<QQuickWidget *>().size(), 4);
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
        QTRY_VERIFY(docks[2]->geometry().right() < docks[3]->geometry().left());
        for (int i = 2; i < docks.size(); ++i) {
            QVERIFY(docks[i]->geometry().top() > window.centralWidget()->geometry().bottom());
            QCOMPARE(docks[i]->geometry().top(), docks[2]->geometry().top());
            QCOMPARE(docks[i]->geometry().bottom(), docks[1]->geometry().bottom());
            QVERIFY(docks[1]->geometry().right() < docks[i]->geometry().left());
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
    QTest::newRow("scenes-dark") << QStringLiteral("ScenesPanel") << 4 << false << QString("dark");
    QTest::newRow("scenes-light") << QStringLiteral("ScenesPanel") << 4 << false << QString("light");
    QTest::newRow("sources-dark") << QStringLiteral("SourcesPanel") << 8 << false << QString("dark");
    QTest::newRow("sources-light") << QStringLiteral("SourcesPanel") << 8 << false << QString("light");
    QTest::newRow("sources-hidden-locked-dark") << QStringLiteral("SourcesPanel") << 8 << true << QString("dark");
    QTest::newRow("sources-hidden-locked-light") << QStringLiteral("SourcesPanel") << 8 << true << QString("light");
    QTest::newRow("mixer-dark") << QStringLiteral("MixerPanel") << 4 << false << QString("dark");
    QTest::newRow("mixer-light") << QStringLiteral("MixerPanel") << 4 << false << QString("light");
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
                          "sourceVisible: %1; sourceLocked: %2 } }")
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
        const bool isolateIcon = panelName == QStringLiteral("ScenesPanel")
            || panelName == QStringLiteral("SourcesPanel")
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

class MainWindowGraphicsTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> m_settings;
    std::unique_ptr<ThemeManager> m_theme;
    static QColor centerPixel(PreviewWidget *view)
    {
        const auto image = view->grabFramebuffer();
        return image.isNull() ? QColor() : image.pixelColor(image.width() / 2, image.height() / 2);
    }
private slots:
    void init()
    {
        m_theme.reset();
        m_settings = std::make_unique<QTemporaryDir>();
        QVERIFY(m_settings->isValid());
        m_theme = std::make_unique<ThemeManager>(m_settings->filePath("settings.ini"));
        QVERIFY(m_theme->isReady());
    }

    void themeChangesKeepPreviewAndQmlAlive()
    {
        QTemporaryDir images;
        QVERIFY(images.isValid());
        QImage transparent(64, 36, QImage::Format_ARGB32);
        transparent.fill(Qt::transparent);
        const auto path = images.filePath("transparent.png");
        QVERIFY(transparent.save(path));
        MainWindow window(*m_theme);
        auto *state = window.findChild<StudioState *>();
        QVERIFY(state);
        const auto source = state->addImageSource(state->currentSceneId(), "Logo", path);
        QVERIFY(!source.isEmpty());
        QVERIFY(state->setSourceVisible(state->currentSceneId(), source, false));
        QVERIFY(state->setSourceLocked(state->currentSceneId(), source, true));
        QVERIFY(state->selectSource(source));
        window.show();
        window.hide();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *view = window.findChild<PreviewWidget *>();
        QVERIFY(view);
        QTRY_VERIFY(view->displayedFrameToken() != 0);
        QTRY_COMPARE(centerPixel(view), QColor(Qt::black));
        const auto panels = window.findChildren<QQuickWidget *>();
        QList<QPointer<QQuickItem>> roots;
        for (auto *panel : panels) {
            QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
            roots.append(panel->rootObject());
        }
        QPointer<QOpenGLContext> context = view->context();
        for (const auto &id : {"light", "dark"}) {
            QVERIFY(m_theme->setTheme(id));
            QTRY_COMPARE(window.palette().color(QPalette::Window), m_theme->color("background.window"));
            QTRY_COMPARE(centerPixel(view), QColor(Qt::black));
            QCOMPARE(view->context(), context.data());
            for (int i = 0; i < panels.size(); ++i) {
                QCOMPARE(panels[i]->rootObject(), roots[i].data());
                QCOMPARE(panels[i]->quickWindow()->color(), m_theme->color("background.panel"));
                QCOMPARE(panels[i]->rootObject()->property("color").value<QColor>(), m_theme->color("background.panel"));
            }
            QTest::qWait(100);
            QVERIFY(window.grab().save(QCoreApplication::applicationDirPath()
                + QStringLiteral("/theme-3c-%1.png").arg(id)));
        }
    }

    void sceneCompositionAndRecovery()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto redPath = directory.filePath("red.png");
        const auto bluePath = directory.filePath("blue.png");
        QImage red(1920, 1080, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QVERIFY(red.save(redPath));
        QImage blue(1920, 1080, QImage::Format_ARGB32);
        blue.fill(QColor(0, 0, 255, 128));
        QVERIFY(blue.save(bluePath));
        MainWindow window(*m_theme);
        auto *state = window.findChild<StudioState *>();
        auto *view = window.findChild<PreviewWidget *>();
        auto *status = window.findChild<QLabel *>("previewStatus");
        QVERIFY(state && view && status);
        window.show();
        // CTest's hidden Windows launcher can suppress the first native ShowWindow.
        window.hide();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY(view->isReady());
        QTRY_VERIFY(view->displayedFrameToken() != 0);
        for (auto *panel : window.findChildren<QQuickWidget *>()) {
            QTRY_COMPARE(panel->status(), QQuickWidget::Ready);
            QVERIFY(panel->rootObject());
        }
        qInfo() << "OpenGL" << view->context()->format() << "DPR" << view->devicePixelRatioF();
        const auto scene = state->currentSceneId();
        const auto redId = state->addImageSource(scene, "Red", redPath);
        QVERIFY(!redId.isEmpty());
        QTRY_VERIFY(centerPixel(view).red() > 250);
        const auto blueId = state->addImageSource(scene, "<blue>", bluePath);
        QVERIFY(!blueId.isEmpty());
        QTRY_VERIFY(qAbs(centerPixel(view).red() - 127) <= 3
                    && qAbs(centerPixel(view).blue() - 128) <= 3);
        QVERIFY(state->moveSource(scene, blueId, 1));
        QTRY_VERIFY(centerPixel(view).red() > 250 && centerPixel(view).blue() < 3);
        QVERIFY(state->setSourceVisible(scene, redId, false));
        QTRY_VERIFY(centerPixel(view).red() < 3 && qAbs(centerPixel(view).blue() - 128) <= 3);
        QVERIFY(state->setSourceVisible(scene, redId, true));
        QVERIFY(state->moveSource(scene, blueId, -1));
        QTRY_VERIFY(centerPixel(view).blue() > 120);
        QVERIFY(QFile::remove(bluePath));
        QVERIFY(state->setSourceVisible(scene, blueId, false));
        QVERIFY(state->setSourceVisible(scene, blueId, true));
        QTRY_VERIFY(status->text().contains("<blue>"));
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QTRY_VERIFY(centerPixel(view).red() > 250 && centerPixel(view).blue() < 3);
        {
            QFile corrupt(bluePath);
            QVERIFY(corrupt.open(QIODevice::WriteOnly));
            QCOMPARE(corrupt.write("invalid image"), qint64(13));
        }
        window.findChild<SceneSnapshotBuilder *>()->requestSnapshot();
        QTRY_VERIFY(!window.findChild<SceneSnapshotBuilder *>()->isLoading());
        QTRY_VERIFY(status->text().contains("<blue>"));
        QVERIFY(blue.save(bluePath));
        QVERIFY(state->setSourceVisible(scene, blueId, false));
        QVERIFY(state->setSourceVisible(scene, blueId, true));
        QTRY_VERIFY(status->text().isEmpty());
        QTRY_VERIFY(centerPixel(view).blue() > 120);
        const auto empty = state->addScene("Empty");
        QVERIFY(!empty.isEmpty());
        QVERIFY(state->selectScene(empty));
        QTRY_VERIFY(centerPixel(view).red() < 3 && centerPixel(view).blue() < 3);
        QVERIFY(state->selectScene(scene));
        QTRY_VERIFY(centerPixel(view).blue() > 120);
        window.resize(1100, 720);
        QTRY_VERIFY(qAbs(view->canvasRect().width() / view->canvasRect().height() - 16.0 / 9) < .01);
        auto *dock = window.findChild<QDockWidget *>("sourcesDock");
        QVERIFY(dock);
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        dock->setFloating(false);
        dock->close();
        dock->toggleViewAction()->trigger();
        QTRY_VERIFY(dock->isVisible());
        window.showMinimized();
        QTRY_VERIFY(window.isMinimized());
        QVERIFY(state->setSourceVisible(scene, blueId, false));
        window.showNormal();
        QTRY_VERIFY(!window.isMinimized());
        QTRY_VERIFY(centerPixel(view).red() > 250 && centerPixel(view).blue() < 3);
        QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/c13c-mainwindow.png"));

        // A different top-level recreates the widget context when global
        // context sharing is disabled. The assembly must stop and restart too.
        QWidget otherWindow;
        otherWindow.resize(800, 500);
        auto *pages = qobject_cast<QStackedWidget *>(view->parentWidget());
        QVERIFY(pages);
        QSignalSpy released(view, &PreviewWidget::contextAboutToBeReleased);
        QSignalSpy ready(view, &PreviewWidget::contextReady);
        view->setParent(&otherWindow);
        view->resize(otherWindow.size());
        view->show();
        otherWindow.show();
        QVERIFY(QTest::qWaitForWindowExposed(&otherWindow));
        QTRY_VERIFY(!released.isEmpty() && !ready.isEmpty());
        auto *failure = window.findChild<QLabel *>("previewUnavailable");
        QVERIFY(failure);
        QTRY_VERIFY(!failure->text().isEmpty() || view->displayedFrameToken() != 0);
        if (failure->text().isEmpty())
            QTRY_VERIFY(centerPixel(view).red() > 250);
        pages->addWidget(view);
        pages->setCurrentWidget(view);
        view->show();
        QTRY_VERIFY(released.size() >= 2 && ready.size() >= 2);
        // Some WGL drivers reject sharing back into the original Qt Quick
        // top-level. In that case the real error must replace the picture.
        QTRY_VERIFY(failure->isVisible() || view->displayedFrameToken() != 0);
        if (failure->isVisible()) {
            QVERIFY(!failure->text().isEmpty());
            qInfo().noquote() << "Context recreation fallback:" << failure->text();
        } else {
            QTRY_VERIFY(centerPixel(view).red() > 250);
        }
    }

    void repeatedShutdown()
    {
        for (int i = 0; i < 3; ++i) {
            auto window = std::make_unique<MainWindow>(*m_theme);
            QPointer<StudioState> state = window->findChild<StudioState *>();
            QPointer<PreviewWidget> view = window->findChild<PreviewWidget *>();
            QPointer<CompositionSession> session = window->findChild<CompositionSession *>();
            QPointer<SceneSnapshotBuilder> builder = window->findChild<SceneSnapshotBuilder *>();
            QVERIFY(state && view && session && builder);
            window->show();
            QVERIFY(QTest::qWaitForWindowExposed(window.get()));
            QTRY_VERIFY(view->displayedFrameToken() != 0);
            builder->requestSnapshot();
            window.reset();
            QVERIFY(!state && !view && !session && !builder);
        }
    }
};

int main(int argc, char **argv)
{
    QStringList arguments;
    bool graphics = false;
    for (int i = 0; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--graphics"))
            graphics = true;
        else
            arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    if (graphics) {
        qRegisterMetaType<QOpenGLContext *>();
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setSamples(0);
        QSurfaceFormat::setDefaultFormat(format);
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    }
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (graphics) {
        MainWindowGraphicsTest test;
        return QTest::qExec(&test, arguments);
    }
    MainWindowTest test;
    return QTest::qExec(&test, arguments);
}
#include "tst_mainwindow.moc"
