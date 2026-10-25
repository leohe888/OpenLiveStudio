#include "studiostate.h"

#include <QPersistentModelIndex>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

class StudioStateTest : public QObject
{
    Q_OBJECT
private slots:
    void startsWithOneEmptyScene();
    void exposesSeparateSceneAndSourceRoles();
    void switchesSourcesAndClearsSelection();
    void rejectsInvalidSelectionAndIgnoresRepeatedSelection();
    void supportsEmptyInitialState();
    void addsEmptySceneAndPreservesPreviousContent();
    void suggestsUnusedSceneNames();
    void suggestsUnusedSourceNamesWithinTargetScene();
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
    void addsImageSourcesAndPreservesSceneIsolation();
    void rejectsInvalidImageSources_data();
    void rejectsInvalidImageSources();
    void removesSelectedSource_data();
    void removesSelectedSource();
    void removesSourceWithoutChangingOtherSelections();
    void rejectsInvalidSourceRemoval();
    void removesLastImageSourceWithoutDeletingFile();
    void movesSourcesAndPreservesIdentity();
    void sourceFlagsStayWithSceneEntries();
    void sourceMoveAvailabilityTracksChanges();
    void sameImageEntriesStayIndependentAcrossSceneChanges();
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

void StudioStateTest::exposesSeparateSceneAndSourceRoles()
{
    StudioState state({{"scene", "Scene", {{"source", "Image", "image", "fixture.png",
                                           QSize(64, 32), false, true}}}});
    auto *sceneModel = state.sceneModel();
    auto *sourceModel = state.sourceModel();
    const QHash<int, QByteArray> sceneRoles{{StudioState::IdRole, "sceneId"},
                                          {StudioState::NameRole, "sceneName"}};
    const QHash<int, QByteArray> sourceRoles{
        {StudioState::IdRole, "sourceId"}, {StudioState::NameRole, "sourceName"},
        {StudioState::TypeRole, "sourceType"}, {StudioState::FilePathRole, "sourceFilePath"},
        {StudioState::OriginalSizeRole, "sourceOriginalSize"},
        {StudioState::VisibleRole, "sourceVisible"}, {StudioState::LockedRole, "sourceLocked"}};
    QCOMPARE(sceneModel->roleNames(), sceneRoles);
    QCOMPARE(sourceModel->roleNames(), sourceRoles);

    const auto scene = sceneModel->index(0, 0);
    const auto source = sourceModel->index(0, 0);
    for (const int role : {StudioState::TypeRole, StudioState::FilePathRole,
                           StudioState::OriginalSizeRole, StudioState::VisibleRole,
                           StudioState::LockedRole}) {
        QVERIFY(!scene.data(role).isValid());
        QVERIFY(source.data(role).isValid());
    }
    QCOMPARE(scene.data(Qt::DisplayRole).toString(), QString("Scene"));
    QCOMPARE(source.data(Qt::DisplayRole).toString(), QString("Image"));
    QCOMPARE(source.data(StudioState::TypeRole).toString(), QString("image"));
    QCOMPARE(source.data(StudioState::FilePathRole).toString(), QString("fixture.png"));
    QCOMPARE(source.data(StudioState::OriginalSizeRole).toSize(), QSize(64, 32));
    QCOMPARE(source.data(StudioState::VisibleRole).toBool(), false);
    QCOMPARE(source.data(StudioState::LockedRole).toBool(), true);
    QVERIFY(!sourceModel->data(scene, StudioState::IdRole).isValid());
    QVERIFY(!sceneModel->data(source, StudioState::IdRole).isValid());
    QVERIFY(!sourceModel->data({}, StudioState::IdRole).isValid());
    QVERIFY(!source.data(Qt::UserRole + 100).isValid());
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

void StudioStateTest::addsImageSourcesAndPreservesSceneIsolation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("image.png");
    QImage image(13, 7, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QVERIFY(image.save(path));
    StudioState state(scenes());
    auto *model = state.sourceModel();
    QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QPersistentModelIndex previous(model->index(0, 0));
    QVERIFY(state.imageSourceError("first", " Logo ", path).isEmpty());
    const QString id = state.addImageSource("first", " Logo ", path);
    QVERIFY(!id.isEmpty());
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), id);
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.first().at(1).toInt(), 0);
    QCOMPARE(inserted.first().at(2).toInt(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(previous.row(), 1);
    QCOMPARE(previous.data(StudioState::IdRole).toString(), QString("a"));
    const auto row = model->index(0, 0);
    QCOMPARE(row.data(StudioState::IdRole).toString(), id);
    QCOMPARE(row.data(StudioState::NameRole).toString(), QString("Logo"));
    QCOMPARE(row.data(StudioState::TypeRole).toString(), QString("image"));
    QCOMPARE(row.data(StudioState::FilePathRole).toString(), path);
    QCOMPARE(row.data(StudioState::OriginalSizeRole).toSize(), QSize(13, 7));
    QCOMPARE(model->roleNames().value(StudioState::OriginalSizeRole), QByteArray("sourceOriginalSize"));
    QVERIFY(!state.sceneModel()->index(0, 0).data(StudioState::TypeRole).isValid());
    // A captured target scene can differ from the current scene without changing selection.
    const QString other = state.addImageSource("second", "Logo", path);
    QVERIFY(!other.isEmpty());
    QVERIFY(other != id);
    QCOMPARE(state.currentSourceId(), id);
    QCOMPARE(inserted.count(), 1);
    QVERIFY(state.selectScene("second"));
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), other);
    QCOMPARE(model->index(0, 0).data(StudioState::OriginalSizeRole).toSize(), image.size());
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(state.selectScene("first"));
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), id);
    QCOMPARE(model->index(0, 0).data(StudioState::FilePathRole).toString(), path);
}

void StudioStateTest::rejectsInvalidImageSources_data()
{
    QTest::addColumn<QString>("failure");
    for (const auto *name : {"empty-name", "duplicate-name", "missing-file", "empty-path",
                             "directory", "corrupt-file", "truncated-image", "missing-scene"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void StudioStateTest::rejectsInvalidImageSources()
{
    QFETCH(QString, failure);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString path = dir.filePath("image.png");
    QImage image(13, 7, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    StudioState state(scenes());
    QVERIFY(state.selectSource("a"));
    QString sceneId = "first", name = "Logo";
    if (failure == "empty-name") name = " \t ";
    if (failure == "duplicate-name") name = " iMaGe ";
    if (failure == "missing-scene") sceneId = "missing";
    if (failure == "missing-file") path = dir.filePath("missing.png");
    if (failure == "empty-path") path.clear();
    if (failure == "directory") path = dir.path();
    if (failure == "corrupt-file" || failure == "truncated-image") {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        if (failure == "corrupt-file") {
            QVERIFY(file.resize(0));
            QCOMPARE(file.write("not an image"), qint64(12));
        } else {
            QVERIFY(file.resize(40)); // Keep PNG header, discard pixel data.
        }
    }
    QSignalSpy inserted(state.sourceModel(), &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(!state.imageSourceError(sceneId, name, path).isEmpty());
    QVERIFY(state.addImageSource(sceneId, name, path).isEmpty());
    QCOMPARE(state.sourceModel()->rowCount(), 2);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(inserted.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    QVERIFY(state.selectScene("second"));
    QCOMPARE(state.sourceModel()->rowCount(), 2);
    StudioState empty(QList<StudioState::SceneData>{});
    QVERIFY(empty.addImageSource({}, "Logo", path).isEmpty());
}

void StudioStateTest::removesSelectedSource_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("first") << 0;
    QTest::newRow("middle") << 1;
    QTest::newRow("last") << 2;
}

void StudioStateTest::removesSelectedSource()
{
    QFETCH(int, row);
    const QList<StudioState::SourceData> sources{{"a", "A"}, {"b", "B"}, {"c", "C"}};
    StudioState state(QList<StudioState::SceneData>{{"scene", "Scene", sources}});
    auto *model = state.sourceModel();
    const QString id = sources.at(row).id;
    QVERIFY(state.selectSource(id));
    QPersistentModelIndex removedIndex(model->index(row, 0));
    QPersistentModelIndex remainingIndex(model->index(row == 0 ? 1 : 0, 0));
    const QString remainingId = remainingIndex.data(StudioState::IdRole).toString();
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy sceneSelected(&state, &StudioState::currentSceneIdChanged);
    QVERIFY(state.removeSource("scene", id));
    QCOMPARE(state.sourceModel(), model);
    QCOMPARE(model->rowCount(), 2);
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.currentSceneId(), QString("scene"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(1).toInt(), row);
    QCOMPARE(removed.first().at(2).toInt(), row);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(sceneSelected.count(), 0);
    QVERIFY(!removedIndex.isValid());
    QVERIFY(remainingIndex.isValid());
    QCOMPARE(remainingIndex.data(StudioState::IdRole).toString(), remainingId);
    QVERIFY(!state.selectSource(id));
    QVERIFY(!state.removeSource("scene", id));
}

void StudioStateTest::removesSourceWithoutChangingOtherSelections()
{
    StudioState state(scenes());
    QVERIFY(state.selectSource("shared"));
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy removed(state.sourceModel(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QVERIFY(state.removeSource("first", "a"));
    QCOMPARE(state.currentSourceId(), QString("shared"));
    QCOMPARE(removed.count(), 1);
    removed.clear();
    // A shared ID in another scene must not affect the selected scene's entry.
    QVERIFY(state.removeSource("second", "shared"));
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("shared"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    QVERIFY(state.selectScene("second"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QCOMPARE(state.sourceModel()->index(0, 0).data(StudioState::IdRole).toString(), QString("b"));
    QVERIFY(!state.selectSource("shared"));
    QVERIFY(state.selectScene("first"));
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QVERIFY(state.selectSource("shared"));
}

void StudioStateTest::rejectsInvalidSourceRemoval()
{
    StudioState state(scenes());
    QVERIFY(state.selectSource("a"));
    QSignalSpy removed(state.sourceModel(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(state.sourceModel(), &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(!state.removeSource({}, "a"));
    QVERIFY(!state.removeSource("missing", "a"));
    QVERIFY(!state.removeSource("first", {}));
    QVERIFY(!state.removeSource("first", "missing"));
    QVERIFY(!state.removeSource("second", "a"));
    QVERIFY(!state.removeSource("empty", "a"));
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(state.sourceModel()->rowCount(), 2);
    QCOMPARE(removed.count(), 0);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    StudioState empty(QList<StudioState::SceneData>{});
    QVERIFY(!empty.removeSource("missing", "a"));
}

void StudioStateTest::removesLastImageSourceWithoutDeletingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("image.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    StudioState state;
    const QString sceneId = state.currentSceneId();
    const QString id = state.addImageSource(sceneId, "Logo", path);
    QVERIFY(!id.isEmpty());
    QVERIFY(state.removeSource(sceneId, id));
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QCOMPARE(state.sceneModel()->rowCount(), 1);
    QCOMPARE(state.currentSceneId(), sceneId);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
    QVERIFY(!state.removeSource(sceneId, id));
    const QString newId = state.addImageSource(sceneId, "Logo", path);
    QVERIFY(!newId.isEmpty());
    QVERIFY(newId != id);
    QCOMPARE(state.currentSourceId(), newId);
}

void StudioStateTest::suggestsUnusedSourceNamesWithinTargetScene()
{
    const QString base = QString::fromUtf8("图像");
    StudioState state(QList<StudioState::SceneData>{
        {"first", "First", {{"a", base}, {"b", base + " 3"},
                            {"c", "Camera"}, {"d", " cAmErA 2 "}}},
        {"second", "Second", {}}});
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QSignalSpy inserted(state.sourceModel(), &QAbstractItemModel::rowsInserted);
    QCOMPARE(state.suggestedSourceName("first", base), base + " 2");
    QCOMPARE(state.suggestedSourceName("first", " camera "), QString("camera 3"));
    QCOMPARE(state.suggestedSourceName("second", base), base);
    QVERIFY(state.suggestedSourceName("missing", base).isEmpty());
    QVERIFY(state.suggestedSourceName({}, base).isEmpty());
    QVERIFY(state.suggestedSourceName("first", " \t ").isEmpty());
    QCOMPARE(state.currentSceneId(), QString("first"));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(state.sourceModel()->rowCount(), 4);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QVERIFY(state.selectScene("second"));
    QCOMPARE(state.suggestedSourceName("first", base), base + " 2");
    QVERIFY(state.removeSource("first", "a"));
    QCOMPARE(state.suggestedSourceName("first", base), base);
}

void StudioStateTest::movesSourcesAndPreservesIdentity()
{
    StudioState state(scenes());
    auto *model = state.sourceModel();
    QVERIFY(state.selectSource("a"));
    QVERIFY(state.setSourceVisible("first", "a", false));
    QVERIFY(state.setSourceLocked("first", "a", true));
    QPersistentModelIndex original(model->index(0, 0));
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(!state.moveSource("first", "a", -1));
    QVERIFY(!state.moveSource("first", "shared", 1));
    QVERIFY(!state.moveSource("first", "a", 0));
    QVERIFY(!state.moveSource("first", "a", 2));
    QVERIFY(!state.moveSource("missing", "a", 1));
    QVERIFY(!state.moveSource("first", "missing", 1));
    QVERIFY(!state.moveSource("first", {}, 1));
    QVERIFY(!state.moveSource("empty", "a", 1));
    QVERIFY(!state.moveSource("second", "a", 1));
    QCOMPARE(moved.count(), 0);
    QVERIFY(state.moveSource("first", "a", 1));
    QCOMPARE(original.row(), 1);
    QCOMPARE(original.data(StudioState::IdRole).toString(), QString("a"));
    QCOMPARE(original.data(StudioState::NameRole).toString(), QString("Image"));
    QCOMPARE(original.data(StudioState::VisibleRole).toBool(), false);
    QCOMPARE(original.data(StudioState::LockedRole).toBool(), true);
    QCOMPARE(moved.count(), 1);
    QVERIFY(state.moveSource("first", "shared", 1)); // Move the unselected entry.
    QCOMPARE(original.row(), 0);
    QCOMPARE(moved.count(), 2);
    QVERIFY(state.moveSource("second", "shared", -1)); // Inactive scene only.
    QCOMPARE(moved.count(), 2);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(state.selectScene("second"));
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), QString("shared"));
    QVERIFY(state.selectScene("first"));
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), QString("a"));
    QVERIFY(!model->index(0, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(model->index(0, 0).data(StudioState::LockedRole).toBool());
}

void StudioStateTest::sourceFlagsStayWithSceneEntries()
{
    StudioState state(scenes());
    auto *model = state.sourceModel();
    QCOMPARE(model->roleNames().value(StudioState::VisibleRole), QByteArray("sourceVisible"));
    QCOMPARE(model->roleNames().value(StudioState::LockedRole), QByteArray("sourceLocked"));
    QVERIFY(model->index(1, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(!model->index(1, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(state.selectSource("a"));
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(state.setSourceVisible("first", "shared", false));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(2).value<QList<int>>(), QList<int>{StudioState::VisibleRole});
    QVERIFY(state.setSourceVisible("first", "shared", false));
    QCOMPARE(changed.count(), 1); // Idempotent setters do not notify.
    QVERIFY(state.setSourceLocked("first", "shared", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.last().at(2).value<QList<int>>(), QList<int>{StudioState::LockedRole});
    QVERIFY(!model->index(1, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(model->index(1, 0).data(StudioState::LockedRole).toBool());
    for (const auto &target : QList<QPair<QString, QString>>{
             {"missing", "a"}, {"first", "missing"}, {"first", {}}, {"second", "a"}, {"empty", "a"}}) {
        QVERIFY(!state.setSourceVisible(target.first, target.second, false));
        QVERIFY(!state.setSourceLocked(target.first, target.second, true));
    }
    QVERIFY(state.setSourceVisible("second", "b", false));
    QVERIFY(state.setSourceLocked("second", "b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(state.selectScene("second"));
    QVERIFY(!model->index(0, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(model->index(0, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(model->index(1, 0).data(StudioState::VisibleRole).toBool()); // Same ID, other scene.
    QVERIFY(!model->index(1, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(state.selectScene("first"));
    QVERIFY(state.setSourceVisible("first", "shared", true));
    QVERIFY(state.setSourceLocked("first", "shared", false));
    QVERIFY(model->index(1, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(!model->index(1, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(state.setSourceLocked("first", "shared", true));
    QVERIFY(state.removeSource("first", "shared")); // Locked entries remain removable.
}

void StudioStateTest::sourceMoveAvailabilityTracksChanges()
{
    StudioState state(scenes());
    QSignalSpy available(&state, &StudioState::sourceMoveAvailabilityChanged);
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(state.selectSource("a"));
    QVERIFY(!available.isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(state.canMoveCurrentSourceDown());
    available.clear();
    QVERIFY(state.moveSource("first", "a", 1));
    QVERIFY(!available.isEmpty());
    QVERIFY(state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    available.clear();
    QVERIFY(state.removeSource("first", "shared"));
    QVERIFY(!available.isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QString path = dir.filePath("image.png");
    QVERIFY(image.save(path));
    const QString added = state.addImageSource("first", "New", path);
    QVERIFY(!added.isEmpty());
    QVERIFY(state.sourceModel()->index(0, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(!state.sourceModel()->index(0, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(state.canMoveCurrentSourceDown());
    QVERIFY(state.selectSource({}));
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(state.selectScene("second"));
    QVERIFY(state.selectSource("shared"));
    QVERIFY(state.canMoveCurrentSourceUp());
    available.clear();
    QVERIFY(state.selectScene("empty"));
    QVERIFY(!available.isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
}

void StudioStateTest::sameImageEntriesStayIndependentAcrossSceneChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("shared-file.png");
    QImage image(17, 9, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QVERIFY(image.save(path));
    StudioState state(QList<StudioState::SceneData>{
        {"a", "First", {}}, {"b", "Second", {}}, {"empty", "Empty", {}}});
    const QString a = state.addImageSource("a", "Logo", path);
    const QString aBackground = state.addImageSource("a", "Background", path);
    const QString b = state.addImageSource("b", "Logo", path);
    const QString bBackground = state.addImageSource("b", "Background", path);
    const QSet<QString> ids{a, aBackground, b, bBackground};
    QCOMPARE(ids.size(), 4);
    QVERIFY(!ids.contains(QString()));
    auto *model = state.sourceModel();
    const auto snapshot = [model] {
        QList<QVariantList> rows;
        for (int row = 0; row < model->rowCount(); ++row) {
            QVariantList values;
            for (const auto role : {StudioState::IdRole, StudioState::NameRole,
                     StudioState::TypeRole, StudioState::FilePathRole,
                     StudioState::OriginalSizeRole, StudioState::VisibleRole, StudioState::LockedRole})
                values.append(model->index(row, 0).data(role));
            rows.append(values);
        }
        return rows;
    };
    QVERIFY(state.moveSource("a", a, -1));
    QVERIFY(state.setSourceVisible("a", a, false));
    QVERIFY(state.setSourceLocked("a", a, true));
    QVERIFY(state.selectSource(a));
    const auto first = snapshot();
    QCOMPARE(first.first().at(0).toString(), a);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selection(&state, &StudioState::currentSourceIdChanged);
    QVERIFY(state.selectScene("a"));
    QVERIFY(!state.selectScene("missing"));
    QCOMPARE(state.currentSourceId(), a);
    QCOMPARE(snapshot(), first);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selection.count(), 0);
    QVERIFY(state.selectScene("b"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(reset.count(), 1);
    QCOMPARE(selection.count(), 1);
    QCOMPARE(model->index(0, 0).data(StudioState::IdRole).toString(), bBackground);
    QVERIFY(model->index(1, 0).data(StudioState::VisibleRole).toBool());
    QVERIFY(!model->index(1, 0).data(StudioState::LockedRole).toBool());
    QVERIFY(!state.selectSource(a));
    QVERIFY(state.selectSource(b));
    QVERIFY(state.setSourceVisible("b", bBackground, false));
    QVERIFY(state.setSourceLocked("b", bBackground, true));
    QVERIFY(state.removeSource("b", b));
    QVERIFY(state.currentSourceId().isEmpty());
    const QString bNew = state.addImageSource("b", "New", path);
    QVERIFY(!bNew.isEmpty());
    const auto second = snapshot();
    for (int iteration = 0; iteration < 4; ++iteration) {
        QVERIFY(state.selectScene("a"));
        QVERIFY(state.currentSourceId().isEmpty());
        QCOMPARE(snapshot(), first);
        QVERIFY(state.selectSource(a));
        QVERIFY(state.selectScene("empty"));
        QCOMPARE(model->rowCount(), 0);
        QVERIFY(state.currentSourceId().isEmpty());
        QVERIFY(!state.canMoveCurrentSourceUp());
        QVERIFY(!state.canMoveCurrentSourceDown());
        QVERIFY(state.selectScene("b"));
        QCOMPARE(snapshot(), second);
        QVERIFY(state.currentSourceId().isEmpty());
        QVERIFY(state.selectSource(bNew));
    }
    QVERIFY(state.removeSource("a", a));
    QVERIFY(state.removeScene("a"));
    QCOMPARE(snapshot(), second);
    QCOMPARE(state.currentSourceId(), bNew);
    QCOMPARE(QImage(path), image);
}

QTEST_GUILESS_MAIN(StudioStateTest)
#include "tst_studiostate.moc"
