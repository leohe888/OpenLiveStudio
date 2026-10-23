#include "mainwindow.h"
#include "studiostate.h"

#include <QAction>
#include <QDockWidget>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QImageReader>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlExpression>
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

private slots:
    void init();
    void canShowAndClose();
    void hasWindowShell();
    void panelsShareStateAcrossReloadsAndReleaseItWithWindow();
    void panelsFollowSceneAndSourceSelection();
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
    void sceneRemovalDialogHandlesConfirmation_data();
    void sceneRemovalDialogHandlesConfirmation();
    void scenePanelCanRemoveAndMove_data();
    void scenePanelCanRemoveAndMove();
    void keepsOutputStatusVisibleWhenResized();
    void loadsScenePanelWithEmptyState();
    void loadsSourcePanelWithEmptyState();
    void loadsMixerPanelWithUnavailableChannels();
    void loadsTransitionsPanelWithDisabledControls();
    void loadsControlsPanelWithVerticalButtons();
    void embedsScenePanelInLeftDock();
    void embedsSourcePanelBelowSceneDock();
    void embedsMixerPanelBelowPreview();
    void embedsTransitionsPanelBesideMixerDock();
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

void MainWindowTest::panelsShareStateAcrossReloadsAndReleaseItWithWindow()
{
    auto window = std::make_unique<MainWindow>();
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
    QQuickWidget sources;
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
    MainWindow window;
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

    auto owner = std::make_unique<MainWindow>();
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
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
    MainWindow window;
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

void MainWindowTest::sceneRemovalDialogHandlesConfirmation_data()
{
    QTest::addColumn<QString>("action");
    for (const auto *name : {"confirm", "cancel", "escape", "close", "stale", "last"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void MainWindowTest::sceneRemovalDialogHandlesConfirmation()
{
    QFETCH(QString, action);
    MainWindow window;
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
    MainWindow window;
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
        const QRectF buttonRect = button->mapRectToScene(button->boundingRect());
        QVERIFY(qAbs(buttonRect.bottom() - (root->height() - 8)) <= 1);
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
            QCOMPARE(button->isEnabled(), button->objectName() == QStringLiteral("addSourceButton"));
            QVERIFY(view->rootObject()->boundingRect().contains(
                button->mapRectToScene(button->boundingRect())));
        }
    }
}

void MainWindowTest::embedsMixerPanelBelowPreview()
{
    MainWindow window;
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

void MainWindowTest::embedsTransitionsPanelBesideMixerDock()
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
    QCOMPARE(window.dockWidgetArea(dock), Qt::BottomDockWidgetArea);
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
        QTRY_VERIFY(mixerDock->geometry().right() < dock->geometry().left());
        QCOMPARE(mixerDock->geometry().top(), dock->geometry().top());
        QVERIFY(dock->geometry().top() > window.centralWidget()->geometry().bottom());
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

void MainWindowTest::embedsControlsPanelAtBottomRight()
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
    QTest::newRow("transitions") << QStringLiteral("transitions")
        << Qt::BottomDockWidgetArea << QStringLiteral("TransitionsPanel");
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
        Qt::BottomDockWidgetArea, Qt::BottomDockWidgetArea, Qt::BottomDockWidgetArea};
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
        QTRY_VERIFY(docks[2]->geometry().right() < docks[3]->geometry().left());
        QTRY_VERIFY(docks[3]->geometry().right() < docks[4]->geometry().left());
        for (int i = 2; i < 5; ++i) {
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
    // Test-only data instantiates a row; production starts with an empty source model.
    std::unique_ptr<QObject> sourceModel;
    if (panelName == QStringLiteral("SourcesPanel")) {
        QQmlComponent component(panel.engine());
        component.setData("import QtQml.Models\nListModel { ListElement { "
                          "sourceId: 'image'; sourceName: 'A long source name for layout verification' } }", QUrl());
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
