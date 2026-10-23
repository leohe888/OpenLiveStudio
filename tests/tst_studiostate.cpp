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
    void removesCurrentSceneAndSelectsNeighbour_data();
    void removesCurrentSceneAndSelectsNeighbour();
    void removesOtherSceneWithoutChangingSelection();
    void protectsLastSceneAndUpdatesRemovalAvailability();
    void movesOneRowWithoutChangingSelection_data();
    void movesOneRowWithoutChangingSelection();
    void rejectsInvalidSceneMoves();
    void updatesMoveAvailabilityAfterSceneChanges();
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

void StudioStateTest::removesCurrentSceneAndSelectsNeighbour_data()
{
    QTest::addColumn<int>("row");
    QTest::addColumn<QString>("nextId");
    QTest::newRow("first") << 0 << QString("second");
    QTest::newRow("middle") << 1 << QString("third");
    QTest::newRow("last") << 2 << QString("second");
}

void StudioStateTest::removesCurrentSceneAndSelectsNeighbour()
{
    QFETCH(int, row);
    QFETCH(QString, nextId);
    StudioState state(QList<StudioState::SceneData>{
        {"first", "First", {{"a", "A"}}}, {"second", "Second", {{"b", "B"}}},
        {"third", "Third", {{"c", "C"}}}});
    auto *model = state.sceneModel();
    const QString id = model->index(row, 0).data(StudioState::IdRole).toString();
    QVERIFY(state.selectScene(id));
    QVERIFY(state.selectSource(state.sourceModel()->index(0, 0).data(StudioState::IdRole).toString()));
    QPersistentModelIndex deleted(model->index(row, 0));
    QPersistentModelIndex survivor(model->index(row == 0 ? 1 : 0, 0));
    const QString survivorId = survivor.data(StudioState::IdRole).toString();
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy sceneReset(model, &QAbstractItemModel::modelReset);
    QSignalSpy sourceReset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(state.removeScene(id));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(state.currentSceneId(), nextId);
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    const QString sourceId = state.sourceModel()->index(0, 0).data(StudioState::IdRole).toString();
    QCOMPARE(sourceId, nextId == "second" ? QString("b") : QString("c"));
    QVERIFY(!state.selectScene(id));
    QVERIFY(!deleted.isValid());
    QVERIFY(survivor.isValid());
    QCOMPARE(survivor.data(StudioState::IdRole).toString(), survivorId);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(1).toInt(), row);
    QCOMPARE(removed.first().at(2).toInt(), row);
    QCOMPARE(sceneReset.count(), 0);
    QCOMPARE(sourceReset.count(), 1);
    QCOMPARE(sceneChanged.count(), 1);
    QCOMPARE(sourceChanged.count(), 1);
}

void StudioStateTest::removesOtherSceneWithoutChangingSelection()
{
    StudioState state(scenes());
    QVERIFY(state.selectScene("second"));
    QVERIFY(state.selectSource("b"));
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QVERIFY(state.removeScene("first"));
    QCOMPARE(state.currentSceneId(), QString("second"));
    QCOMPARE(state.currentSourceId(), QString("b"));
    QCOMPARE(state.sourceModel()->rowCount(), 2);
    QCOMPARE(sceneChanged.count(), 0);
    QCOMPARE(sourceChanged.count(), 0);
    QCOMPARE(reset.count(), 0);
}

void StudioStateTest::protectsLastSceneAndUpdatesRemovalAvailability()
{
    StudioState state(QList<StudioState::SceneData>{{"last", "Last", {{"image", "Image"}}}});
    QVERIFY(state.selectSource("image"));
    QVERIFY(!state.canRemoveCurrentScene());
    QSignalSpy availability(&state, &StudioState::canRemoveCurrentSceneChanged);
    QSignalSpy removed(state.sceneModel(), &QAbstractItemModel::rowsRemoved);
    QVERIFY(!state.removeScene("last"));
    QVERIFY(!state.removeScene("missing"));
    QCOMPARE(state.currentSourceId(), QString("image"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    const QString added = state.addScene("New");
    QVERIFY(!added.isEmpty());
    QVERIFY(state.canRemoveCurrentScene());
    QCOMPARE(availability.count(), 1);
    QVERIFY(!state.removeScene({}));
    QVERIFY(!state.removeScene("missing"));
    QCOMPARE(removed.count(), 0);
    QVERIFY(state.removeScene(added));
    QVERIFY(!state.canRemoveCurrentScene());
    QCOMPARE(availability.count(), 2);
    QCOMPARE(state.currentSceneId(), QString("last"));
    QVERIFY(state.selectSource("image"));
    QVERIFY(!state.removeScene("last"));
    QVERIFY(!state.removeScene(added));
    QCOMPARE(state.currentSourceId(), QString("image"));
    QCOMPARE(removed.count(), 1);
    StudioState empty(QList<StudioState::SceneData>{});
    QVERIFY(!empty.canRemoveCurrentScene());
    QVERIFY(!empty.removeScene("missing"));
}

void StudioStateTest::movesOneRowWithoutChangingSelection_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<int>("offset");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("selected-up") << QString("second") << -1 << QStringList{"second", "first", "empty"};
    QTest::newRow("selected-down") << QString("second") << 1 << QStringList{"first", "empty", "second"};
    QTest::newRow("other-down") << QString("first") << 1 << QStringList{"second", "first", "empty"};
}

void StudioStateTest::movesOneRowWithoutChangingSelection()
{
    QFETCH(QString, id);
    QFETCH(int, offset);
    QFETCH(QStringList, expected);
    StudioState state(scenes());
    QVERIFY(state.selectScene("second"));
    QVERIFY(state.selectSource("b"));
    auto *model = state.sceneModel();
    QList<QPersistentModelIndex> original;
    for (int row = 0; row < model->rowCount(); ++row)
        original.append(model->index(row, 0));
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy sourceReset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QSignalSpy sceneChanged(&state, &StudioState::currentSceneIdChanged);
    QSignalSpy sourceChanged(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(state.moveScene(id, offset));
    QStringList actual;
    for (int row = 0; row < model->rowCount(); ++row)
        actual.append(model->index(row, 0).data(StudioState::IdRole).toString());
    QCOMPARE(actual, expected);
    for (int row = 0; row < original.size(); ++row) {
        QVERIFY(original[row].isValid());
        const QString originalId = scenes().at(row).id;
        QCOMPARE(original[row].data(StudioState::IdRole).toString(), originalId);
        QCOMPARE(original[row].row(), expected.indexOf(originalId));
    }
    QCOMPARE(moved.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(sourceReset.count(), 0);
    QCOMPARE(sceneChanged.count(), 0);
    QCOMPARE(sourceChanged.count(), 0);
    QCOMPARE(state.currentSceneId(), QString("second"));
    QCOMPARE(state.currentSourceId(), QString("b"));
    QVERIFY(state.selectScene("first"));
    QVERIFY(state.selectSource("a"));
    QVERIFY(!state.selectSource("b"));
}

void StudioStateTest::rejectsInvalidSceneMoves()
{
    StudioState state(scenes());
    QSignalSpy moved(state.sceneModel(), &QAbstractItemModel::rowsMoved);
    QSignalSpy available(&state, &StudioState::sceneMoveAvailabilityChanged);
    QVERIFY(!state.moveScene("first", -1));
    QVERIFY(!state.moveScene("empty", 1));
    QVERIFY(!state.moveScene("missing", 1));
    QVERIFY(!state.moveScene({}, -1));
    QVERIFY(!state.moveScene("second", 0));
    QVERIFY(!state.moveScene("second", 2));
    QCOMPARE(moved.count(), 0);
    QCOMPARE(available.count(), 0);
    QCOMPARE(state.currentSceneId(), QString("first"));
    for (int row = 0; row < 3; ++row)
        QCOMPARE(state.sceneModel()->index(row, 0).data(StudioState::IdRole).toString(), scenes().at(row).id);
    StudioState single;
    QVERIFY(!single.moveScene(single.currentSceneId(), 1));
    QVERIFY(!single.moveScene(single.currentSceneId(), -1));
    StudioState empty(QList<StudioState::SceneData>{});
    QVERIFY(!empty.moveScene({}, 1));
    QVERIFY(!empty.canMoveCurrentSceneUp());
    QVERIFY(!empty.canMoveCurrentSceneDown());
}

void StudioStateTest::updatesMoveAvailabilityAfterSceneChanges()
{
    StudioState state;
    const QString first = state.currentSceneId();
    QVERIFY(!state.canMoveCurrentSceneUp());
    QVERIFY(!state.canMoveCurrentSceneDown());
    QSignalSpy available(&state, &StudioState::sceneMoveAvailabilityChanged);
    const QString second = state.addScene("Second");
    QVERIFY(!second.isEmpty());
    QVERIFY(!available.isEmpty());
    QVERIFY(state.canMoveCurrentSceneUp());
    QVERIFY(!state.canMoveCurrentSceneDown());
    available.clear();
    QVERIFY(state.selectScene(first));
    QVERIFY(!available.isEmpty());
    QVERIFY(!state.canMoveCurrentSceneUp());
    QVERIFY(state.canMoveCurrentSceneDown());
    available.clear();
    QVERIFY(state.moveScene(second, -1));
    QVERIFY(!available.isEmpty());
    QVERIFY(state.canMoveCurrentSceneUp());
    QVERIFY(!state.canMoveCurrentSceneDown());
    available.clear();
    QVERIFY(state.removeScene(second));
    QVERIFY(!available.isEmpty());
    QVERIFY(!state.canMoveCurrentSceneUp());
    QVERIFY(!state.canMoveCurrentSceneDown());
}

QTEST_GUILESS_MAIN(StudioStateTest)
#include "tst_studiostate.moc"
