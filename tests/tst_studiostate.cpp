#include "studiostate.h"

#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

class StudioStateTest : public QObject
{
    Q_OBJECT
private slots:
    void startsWithOneEmptyScene();
    void switchesSourcesAndClearsSelection();
    void rejectsInvalidSelectionAndIgnoresRepeatedSelection();
    void supportsEmptyInitialState();
    void addsEmptySceneAndPreservesPreviousContent();
    void suggestsUnusedSceneNames();
    void rejectsInvalidNames_data();
    void rejectsInvalidNames();
    void renamesWithoutChangingIdentityOrSelection();
};

static QList<StudioState::SceneData> scenes()
{
    return {{"first", "Same scene name", {{"a", "Image"}, {"shared", "Shared image"}}},
            {"second", "Same scene name", {{"b", "Image"}, {"shared", "Shared image"}}},
            {"empty", "Empty", {}}};
}

void StudioStateTest::startsWithOneEmptyScene()
{
    StudioState state;
    auto *model = state.sceneModel();
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->columnCount(), 1);
    QVERIFY(!model->parent(model->index(0, 0)).isValid());
    QCOMPARE(model->rowCount(model->index(0, 0)), 0);
    QVERIFY(!model->index(1, 0).isValid());
    QVERIFY(!model->index(0, 1).isValid());
    QVERIFY(!state.currentSceneId().isEmpty());
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), state.currentSceneId());
    QCOMPARE(model->index(0, 0).data(StudioState::NameRole).toString(), QString::fromUtf8("场景"));
    QCOMPARE(model->roleNames().value(StudioState::IdRole), QByteArray("sceneId"));
    QCOMPARE(model->roleNames().value(StudioState::NameRole), QByteArray("sceneName"));
    QCOMPARE(state.sourceModel()->roleNames().value(StudioState::IdRole), QByteArray("sourceId"));
    QCOMPARE(state.sourceModel()->roleNames().value(StudioState::NameRole), QByteArray("sourceName"));
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!model->data(QModelIndex(), StudioState::NameRole).isValid());
    QVERIFY(!model->index(0, 0).data(Qt::UserRole + 100).isValid());
}

void StudioStateTest::switchesSourcesAndClearsSelection()
{
    StudioState state(scenes());
    auto *model = state.sourceModel();
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QPersistentModelIndex oldIndex(model->index(0, 0));
    QVERIFY(state.selectSource("shared"));
    QCOMPARE(state.currentSourceId(), QString("shared"));
    // Even a source reused by another scene loses selection when switching.
    QVERIFY(state.selectScene("second"));
    QCOMPARE(state.currentSceneId(), QString("second"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(model, state.sourceModel());
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), QString("b"));
    QVERIFY(!oldIndex.isValid());
    QCOMPARE(sceneChanged.count(), 1);
    QCOMPARE(sourceChanged.count(), 2);
    QCOMPARE(reset.count(), 1);
    QVERIFY(!state.selectSource("a"));
    QVERIFY(state.selectSource("b"));
    QVERIFY(state.selectScene("empty"));
    QCOMPARE(model->rowCount(), 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(state.selectScene("first"));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), QString("a"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.sceneModel()->rowCount(), 3);
}

void StudioStateTest::rejectsInvalidSelectionAndIgnoresRepeatedSelection()
{
    StudioState state(scenes());
    QVERIFY(state.selectSource("a"));
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QVERIFY(!state.selectScene("missing"));
    QVERIFY(!state.selectScene({}));
    QVERIFY(!state.selectSource("b"));
    QVERIFY(state.selectScene("first"));
    QVERIFY(state.selectSource("a"));
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(sceneChanged.count(), 0);
    QCOMPARE(sourceChanged.count(), 0);
    QCOMPARE(reset.count(), 0);
    QVERIFY(state.selectSource({}));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(sourceChanged.count(), 1);
    QVERIFY(state.selectSource({}));
    QCOMPARE(sourceChanged.count(), 1);
}

void StudioStateTest::supportsEmptyInitialState()
{
    StudioState state(QList<StudioState::SceneData>{});
    QCOMPARE(state.sceneModel()->rowCount(), 0);
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QVERIFY(state.currentSceneId().isEmpty());
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!state.selectScene("first"));
    QVERIFY(!state.selectSource("a"));
    QVERIFY(state.selectSource({}));
}

void StudioStateTest::addsEmptySceneAndPreservesPreviousContent()
{
    StudioState state(scenes());
    QVERIFY(state.selectSource("a"));
    auto *model = state.sceneModel();
    QPersistentModelIndex original(model->index(0, 0));
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    const QString id = state.addScene("  New scene  ");
    QVERIFY(!id.isEmpty());
    QCOMPARE(state.currentSceneId(), id);
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(model->index(3, 0).data(StudioState::NameRole).toString(), QString("New scene"));
    QCOMPARE(model->index(3, 0).data(StudioState::IdRole).toString(), id);
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 3);
    QCOMPARE(inserted.first().at(2).toInt(), 3);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(sceneChanged.count(), 1);
    QCOMPARE(sourceChanged.count(), 1);
    QVERIFY(original.isValid());
    QVERIFY(original.data(StudioState::IdRole).toString() != id);
    QVERIFY(state.selectScene("first"));
    QCOMPARE(state.sourceModel()->rowCount(), 2);
    QVERIFY(state.selectSource("a"));
}

void StudioStateTest::suggestsUnusedSceneNames()
{
    StudioState state(QList<StudioState::SceneData>{});
    const QString base = state.suggestedSceneName();
    QVERIFY(!base.isEmpty());
    QVERIFY(!state.addScene(base).isEmpty());
    QVERIFY(!state.addScene(base + " 3").isEmpty());
    QCOMPARE(state.suggestedSceneName(), base + " 2");
    const QString id = state.addScene(state.suggestedSceneName());
    QVERIFY(!id.isEmpty());
    QCOMPARE(state.suggestedSceneName(), base + " 4");
    QVERIFY(state.renameScene(id, "Other"));
    QCOMPARE(state.suggestedSceneName(), base + " 2");
}

void StudioStateTest::rejectsInvalidNames_data()
{
    QTest::addColumn<QString>("name");
    QTest::newRow("empty") << QString();
    QTest::newRow("whitespace") << QString(" \t\n ");
    QTest::newRow("duplicate") << QString("Beta");
    QTest::newRow("duplicate-case") << QString("bETA");
    QTest::newRow("duplicate-trimmed") << QString("  Beta  ");
}

void StudioStateTest::rejectsInvalidNames()
{
    QFETCH(QString, name);
    StudioState state(QList<StudioState::SceneData>{
        {"first", "Alpha", {{"image", "Image"}}}, {"second", "Beta", {}}});
    QVERIFY(state.selectSource("image"));
    QSignalSpy inserted(state.sceneModel(), &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(state.sceneModel(), &QAbstractItemModel::dataChanged);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QVERIFY(!state.sceneNameError(name).isEmpty());
    QVERIFY(state.addScene(name).isEmpty());
    QVERIFY(!state.renameScene("first", name));
    QVERIFY(!state.renameScene("missing", "Valid"));
    QCOMPARE(state.sceneModel()->rowCount(), 2);
    QCOMPARE(state.sceneModel()->index(0, 0).data(StudioState::NameRole).toString(), QString("Alpha"));
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("image"));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(reset.count(), 0);
}

void StudioStateTest::renamesWithoutChangingIdentityOrSelection()
{
    StudioState state(QList<StudioState::SceneData>{
        {"first", "Alpha", {{"image", "Image"}}}, {"second", "Beta", {}}});
    QVERIFY(state.selectSource("image"));
    QPersistentModelIndex original(state.sceneModel()->index(0, 0));
    QSignalSpy changed(state.sceneModel(), &QAbstractItemModel::dataChanged);
    QSignalSpy sceneReset(state.sceneModel(), &QAbstractItemModel::modelReset);
    QSignalSpy sourceReset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QSignalSpy sceneSelection(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceSelection(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(state.sceneNameError(" ALPHA ", "first").isEmpty());
    QVERIFY(state.renameScene("first", "  Renamed  "));
    QVERIFY(original.isValid());
    QCOMPARE(original.data(StudioState::NameRole).toString(), QString("Renamed"));
    QCOMPARE(original.data(Qt::DisplayRole).toString(), QString("Renamed"));
    QCOMPARE(original.data(StudioState::IdRole).toString(), QString("first"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(state.renameScene("first", "Renamed"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(state.renameScene("second", "BETA"));
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("image"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QCOMPARE(sceneReset.count(), 0);
    QCOMPARE(sourceReset.count(), 0);
    QCOMPARE(sceneSelection.count(), 0);
    QCOMPARE(sourceSelection.count(), 0);
}

QTEST_GUILESS_MAIN(StudioStateTest)
#include "tst_studiostate.moc"
