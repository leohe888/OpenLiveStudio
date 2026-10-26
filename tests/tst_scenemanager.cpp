#include "scenemanager.h"

#include <QAbstractItemModelTester>
#include <QFile>
#include <QImage>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

class SceneManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void startsWithEmptySourceList();
    void exposesSourceRolesAndRejectsInvalidIndexes();
    void supportsAllSourceConfigurations();
    void selectionNotifiesOnlyOnChange();
    void removesSelectedSource_data();
    void removesSelectedSource();
    void removalNotifiesBeforeAndAfterMutation();
    void movesSourcesAndPreservesIdentity();
    void flagsNotifyOnlyChangedRole();
    void rejectsInvalidOperations();
    void suggestsUnusedSourceNames();
    void sameImageEntriesRemainIndependent();
    void dialogAdaptersShareSourceState();
};

static QList<SceneManager::SourceData> sources()
{
    return {{"a", "Image"}, {"b", "Camera"}, {"c", "Text"}};
}

void SceneManagerTest::startsWithEmptySourceList()
{
    SceneManager state;
    QAbstractItemModelTester tester(state.sourceModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(!state.currentSceneId().isEmpty()); // Temporary QML adapter remains usable even with no sources.
    QCOMPARE(state.suggestedSourceName("Camera"), QString("Camera"));
    QVERIFY(!state.selectSource("missing"));
    QVERIFY(state.selectSource({}));
    QVERIFY(state.metaObject()->indexOfProperty("sceneModel") < 0);
    for (const auto *method : {"selectScene(QString)", "addScene(QString)",
             "renameScene(QString,QString)", "removeScene(QString)", "moveScene(QString,int)"})
        QVERIFY(state.metaObject()->indexOfMethod(method) < 0);
}

void SceneManagerTest::exposesSourceRolesAndRejectsInvalidIndexes()
{
    SceneManager state(QList<SceneManager::SourceData>{
        {"image", "Logo", SceneManager::ImageConfig{"fixture.png"}, {QSize(64, 32)}, false, true}});
    auto *model = state.sourceModel();
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QHash<int, QByteArray> roles{
        {SceneManager::IdRole, "sourceId"}, {SceneManager::NameRole, "sourceName"},
        {SceneManager::TypeRole, "sourceType"}, {SceneManager::FilePathRole, "sourceFilePath"},
        {SceneManager::OriginalSizeRole, "sourceOriginalSize"},
        {SceneManager::VisibleRole, "sourceVisible"}, {SceneManager::LockedRole, "sourceLocked"}};
    QCOMPARE(model->roleNames(), roles);
    const auto index = model->index(0, 0);
    QCOMPARE(index.data(SceneManager::IdRole).toString(), QString("image"));
    QCOMPARE(index.data(Qt::DisplayRole).toString(), QString("Logo"));
    QCOMPARE(index.data(SceneManager::FilePathRole).toString(), QString("fixture.png"));
    QCOMPARE(index.data(SceneManager::OriginalSizeRole).toSize(), QSize(64, 32));
    QVERIFY(!index.data(SceneManager::VisibleRole).toBool());
    QVERIFY(index.data(SceneManager::LockedRole).toBool());
    QCOMPARE(model->rowCount(index), 0);
    QVERIFY(!model->data({}, SceneManager::IdRole).isValid());
    QVERIFY(!model->index(1, 0).isValid());
    QVERIFY(!model->index(0, 1).isValid());
    QVERIFY(!index.data(Qt::UserRole + 100).isValid());
    SceneManager other(sources());
    QVERIFY(!model->data(other.sourceModel()->index(0, 0), SceneManager::IdRole).isValid());
}

void SceneManagerTest::supportsAllSourceConfigurations()
{
    using Manager = SceneManager;
    const Manager::SourceData defaultSource;
    QCOMPARE(defaultSource.type(), Manager::SourceType::DisplayCapture);
    const auto *defaultConfig = std::get_if<Manager::DisplayCaptureConfig>(&defaultSource.config);
    QVERIFY(defaultConfig);
    QVERIFY(defaultConfig->displayId.isEmpty());
    QList<Manager::SourceData> sources{
        {"display", "Display", Manager::DisplayCaptureConfig{"monitor-1", false}},
        {"window", "Window", Manager::WindowCaptureConfig{
            "program.exe", "WindowClass", "Window title", true}},
        {"camera", "Camera", Manager::CameraConfig{"camera-1", QSize(1280, 720), 30}},
        {"image", "Image", Manager::ImageConfig{"fixture.png"}, {QSize(64, 32)}},
        {"text", "Text", Manager::TextConfig{"Caption", QFont("Arial", 24), Qt::yellow}}};
    const QList<Manager::SourceType> types{
        Manager::SourceType::DisplayCapture, Manager::SourceType::WindowCapture,
        Manager::SourceType::Camera, Manager::SourceType::Image, Manager::SourceType::Text};
    const QStringList typeNames{"displayCapture", "windowCapture", "camera", "image", "text"};
    Manager state(sources);
    auto *model = state.sourceModel();
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    for (int row = 0; row < sources.size(); ++row) {
        QCOMPARE(sources.at(row).type(), types.at(row));
        const auto index = model->index(row, 0);
        QCOMPARE(index.data(Manager::TypeRole).toString(), typeNames.at(row));
        if (types.at(row) == Manager::SourceType::Image) {
            QCOMPARE(index.data(Manager::FilePathRole).toString(), QString("fixture.png"));
            QCOMPARE(index.data(Manager::OriginalSizeRole).toSize(), QSize(64, 32));
        } else {
            QVERIFY(!index.data(Manager::FilePathRole).isValid());
            QVERIFY(!index.data(Manager::OriginalSizeRole).toSize().isValid());
        }
        QVERIFY(state.selectSource(sources.at(row).id));
        QVERIFY(state.setSourceVisible(sources.at(row).id, false));
        QVERIFY(state.setSourceLocked(sources.at(row).id, true));
    }
    // Type follows config replacement; it cannot retain an independently stored value.
    sources[0].config = Manager::ImageConfig{"replacement.png"};
    QCOMPARE(sources.at(0).type(), Manager::SourceType::Image);
    QCOMPARE(model->index(0, 0).data(Manager::TypeRole).toString(), QString("displayCapture"));
    QVERIFY(state.moveSource("text", -1));
    QCOMPARE(model->index(3, 0).data(Manager::TypeRole).toString(), QString("text"));
    for (int row = 0; row < model->rowCount(); ++row) {
        QVERIFY(!model->index(row, 0).data(Manager::VisibleRole).toBool());
        QVERIFY(model->index(row, 0).data(Manager::LockedRole).toBool());
    }
    QVERIFY(state.removeSource("text"));
    QCOMPARE(model->index(3, 0).data(Manager::TypeRole).toString(), QString("image"));
}

void SceneManagerTest::selectionNotifiesOnlyOnChange()
{
    SceneManager state(sources());
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QVERIFY(state.selectSource("a"));
    QCOMPARE(selected.count(), 1);
    QVERIFY(state.selectSource("a"));
    QVERIFY(!state.selectSource("missing"));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(state.selectSource({}));
    QCOMPARE(selected.count(), 2);
    QVERIFY(state.selectSource({}));
    QCOMPARE(selected.count(), 2);
}

void SceneManagerTest::removesSelectedSource_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("first") << 0;
    QTest::newRow("middle") << 1;
    QTest::newRow("last") << 2;
}

void SceneManagerTest::removesSelectedSource()
{
    QFETCH(int, row);
    SceneManager state(sources());
    auto *model = state.sourceModel();
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QString id = model->index(row, 0).data(SceneManager::IdRole).toString();
    QVERIFY(state.selectSource(id));
    QPersistentModelIndex survivor(model->index((row + 1) % 3, 0));
    const QString survivorId = survivor.data(SceneManager::IdRole).toString();
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QVERIFY(state.removeSource(id));
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.first().at(1).toInt(), row);
    QCOMPARE(removed.first().at(2).toInt(), row);
    QCOMPARE(selected.count(), 1);
    QCOMPARE(reset.count(), 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!state.selectSource(id));
    QCOMPARE(survivor.data(SceneManager::IdRole).toString(), survivorId);
}

void SceneManagerTest::removalNotifiesBeforeAndAfterMutation()
{
    SceneManager state(sources());
    auto *model = state.sourceModel();
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QStringList notifications;
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(model->index(1, 0).data(SceneManager::IdRole).toString(), QString("b"));
        notifications.append("before");
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(1, 0).data(SceneManager::IdRole).toString(), QString("c"));
        notifications.append("after");
    });
    QVERIFY(state.removeSource("b"));
    QCOMPARE(notifications, QStringList({"before", "after"}));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
}

void SceneManagerTest::movesSourcesAndPreservesIdentity()
{
    SceneManager state(sources());
    auto *model = state.sourceModel();
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(state.selectSource("a"));
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(state.canMoveCurrentSourceDown());
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QPersistentModelIndex original(model->index(0, 0));
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QSignalSpy available(&state, &SceneManager::sourceMoveAvailabilityChanged);
    QStringList notifications;
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved, this, [&] {
        QCOMPARE(original.row(), 0);
        notifications.append("before");
    });
    connect(model, &QAbstractItemModel::rowsMoved, this, [&] {
        QCOMPARE(original.row(), 1);
        notifications.append("after");
    });
    QVERIFY(state.moveSource("a", 1));
    QCOMPARE(notifications, QStringList({"before", "after"}));
    QCOMPARE(moved.count(), 1);
    QCOMPARE(available.count(), 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(original.data(SceneManager::IdRole).toString(), QString("a"));
    QCOMPARE(original.data(SceneManager::NameRole).toString(), QString("Image"));
    QVERIFY(!original.data(SceneManager::VisibleRole).toBool());
    QVERIFY(original.data(SceneManager::LockedRole).toBool());
    QVERIFY(state.canMoveCurrentSourceUp());
    QVERIFY(state.canMoveCurrentSourceDown());
    // Removing neighbours updates availability without disturbing selection.
    QVERIFY(state.removeSource("c"));
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(state.removeSource("b"));
    QVERIFY(!state.canMoveCurrentSourceUp());
    QCOMPARE(available.count(), 3);
    QCOMPARE(state.currentSourceId(), QString("a"));
}

void SceneManagerTest::flagsNotifyOnlyChangedRole()
{
    SceneManager state(sources());
    auto *model = state.sourceModel();
    QVERIFY(state.selectSource("a"));
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(2).value<QList<int>>(), QList<int>{SceneManager::VisibleRole});
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.last().at(2).value<QList<int>>(), QList<int>{SceneManager::LockedRole});
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(!model->index(1, 0).data(SceneManager::VisibleRole).toBool());
    QVERIFY(model->index(1, 0).data(SceneManager::LockedRole).toBool());
    QVERIFY(state.removeSource("b")); // Locking does not prevent list removal.
}

void SceneManagerTest::rejectsInvalidOperations()
{
    SceneManager state(sources());
    QVERIFY(state.selectSource("a"));
    auto *model = state.sourceModel();
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    for (const auto &id : {QString(), QString("missing")}) {
        QVERIFY(!state.removeSource(id));
        QVERIFY(!state.moveSource(id, 1));
        QVERIFY(!state.setSourceVisible(id, false));
        QVERIFY(!state.setSourceLocked(id, true));
    }
    for (int offset : {-2, 0, 2}) QVERIFY(!state.moveSource("a", offset));
    QVERIFY(!state.moveSource("a", -1));
    QVERIFY(!state.moveSource("c", 1));
    QCOMPARE(removed.count(), 0);
    QCOMPARE(moved.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(model->rowCount(), 3);
    SceneManager empty;
    QVERIFY(!empty.removeSource("a"));
    QVERIFY(!empty.moveSource("a", 1));
}

void SceneManagerTest::suggestsUnusedSourceNames()
{
    const QString base = QString::fromUtf8("图像");
    SceneManager state(QList<SceneManager::SourceData>{
        {"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}});
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &SceneManager::currentSourceIdChanged);
    QSignalSpy inserted(state.sourceModel(), &QAbstractItemModel::rowsInserted);
    QCOMPARE(state.suggestedSourceName(base), base + " 2");
    QCOMPARE(state.suggestedSourceName(" camera "), QString("camera 3"));
    QVERIFY(state.suggestedSourceName({}).isEmpty());
    QVERIFY(state.suggestedSourceName(" \t ").isEmpty());
    QCOMPARE(state.sourceModel()->rowCount(), 4);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QVERIFY(state.removeSource("a"));
    QCOMPARE(state.suggestedSourceName(base), base);
}

void SceneManagerTest::sameImageEntriesRemainIndependent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("shared.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    QList<SceneManager::SourceData> entries{
        {"a", "First", SceneManager::ImageConfig{path}, {image.size()}},
        {"b", "Second", SceneManager::ImageConfig{path}, {image.size()}}};
    SceneManager state(entries);
    entries.clear(); // State owns its source list.
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto other = state.sourceModel()->index(0, 0);
    QCOMPARE(other.data(SceneManager::IdRole).toString(), QString("b"));
    QCOMPARE(other.data(SceneManager::OriginalSizeRole).toSize(), image.size());
    QVERIFY(other.data(SceneManager::VisibleRole).toBool());
    QVERIFY(!other.data(SceneManager::LockedRole).toBool());
    QVERIFY(state.selectSource("a"));
    QVERIFY(state.removeSource("a"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.sourceModel()->rowCount(), 1);
    QCOMPARE(QImage(path), image);
    QVERIFY(state.removeSource("b"));
    QCOMPARE(state.sourceModel()->rowCount(), 0);
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
}

void SceneManagerTest::dialogAdaptersShareSourceState()
{
    SceneManager state(sources());
    const auto canvas = state.currentSceneId();
    auto *model = state.sourceModel();
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QCOMPARE(state.suggestedSourceName(canvas, "Image"), state.suggestedSourceName("Image"));
    for (const auto &invalid : {QString(), QString("missing")}) {
        QVERIFY(state.suggestedSourceName(invalid, "Image").isEmpty());
        QVERIFY(!state.removeSource(invalid, "a"));
    }
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(removed.count(), 0);
    QVERIFY(state.removeSource(canvas, "a"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(state.suggestedSourceName("Image"), QString("Image"));
    QVERIFY(!state.removeSource("a"));
    QVERIFY(state.removeSource("b"));
    QVERIFY(!state.removeSource(canvas, "b"));
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(removed.count(), 2);
}

QTEST_GUILESS_MAIN(SceneManagerTest)
#include "tst_scenemanager.moc"
