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

QTEST_GUILESS_MAIN(StudioStateTest)
#include "tst_studiostate.moc"
