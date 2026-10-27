#include "sourcelistmodel.h"

#include <QAbstractItemModelTester>
#include <QFile>
#include <QImage>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

class SourceListModelTest : public QObject
{
    Q_OBJECT
private slots:
    void insertsSourcesAtTop();
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
};

static QList<SourceData> sources()
{
    return {{"a", "Image"}, {"b", "Camera"}, {"c", "Text"}};
}

void SourceListModelTest::startsWithEmptySourceList()
{
    SourceListModel state;
    QAbstractItemModelTester tester(&state, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(state.rowCount(), 0);
    QVERIFY(state.metaObject()->indexOfProperty("sourceModel") < 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QCOMPARE(state.suggestedSourceName("Camera"), QString("Camera"));
    QVERIFY(!state.selectSource("missing"));
    QVERIFY(state.selectSource({}));
    QVERIFY(state.metaObject()->indexOfProperty("sceneModel") < 0);
    for (const auto *method : {"selectScene(QString)", "addScene(QString)",
             "renameScene(QString,QString)", "removeScene(QString)", "moveScene(QString,int)"})
        QVERIFY(state.metaObject()->indexOfMethod(method) < 0);
}

void SourceListModelTest::exposesSourceRolesAndRejectsInvalidIndexes()
{
    SourceListModel state(QList<SourceData>{
        {"image", "Logo", ImageConfig{"fixture.png"}, {QSize(64, 32)}, false, true}});
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QHash<int, QByteArray> roles{
        {SourceListModel::IdRole, "sourceId"}, {SourceListModel::NameRole, "sourceName"},
        {SourceListModel::TypeRole, "sourceType"}, {SourceListModel::FilePathRole, "sourceFilePath"},
        {SourceListModel::OriginalSizeRole, "sourceOriginalSize"},
        {SourceListModel::VisibleRole, "sourceVisible"}, {SourceListModel::LockedRole, "sourceLocked"}};
    QCOMPARE(model->roleNames(), roles);
    const auto index = model->index(0, 0);
    QCOMPARE(index.data(SourceListModel::IdRole).toString(), QString("image"));
    QCOMPARE(index.data(Qt::DisplayRole).toString(), QString("Logo"));
    QCOMPARE(index.data(SourceListModel::FilePathRole).toString(), QString("fixture.png"));
    QCOMPARE(index.data(SourceListModel::OriginalSizeRole).toSize(), QSize(64, 32));
    QVERIFY(!index.data(SourceListModel::VisibleRole).toBool());
    QVERIFY(index.data(SourceListModel::LockedRole).toBool());
    QCOMPARE(model->rowCount(index), 0);
    QVERIFY(!model->data({}, SourceListModel::IdRole).isValid());
    QVERIFY(!model->index(1, 0).isValid());
    QVERIFY(!model->index(0, 1).isValid());
    QVERIFY(!index.data(Qt::UserRole + 100).isValid());
    SourceListModel other(sources());
    QVERIFY(!model->data(other.index(0, 0), SourceListModel::IdRole).isValid());
}

void SourceListModelTest::supportsAllSourceConfigurations()
{
    using Manager = SourceListModel;
    const SourceData defaultSource;
    QCOMPARE(defaultSource.type(), SourceType::DisplayCapture);
    const auto *defaultConfig = std::get_if<DisplayCaptureConfig>(&defaultSource.config);
    QVERIFY(defaultConfig);
    QVERIFY(defaultConfig->displayId.isEmpty());
    QList<SourceData> sources{
        {"display", "Display", DisplayCaptureConfig{"monitor-1", false}},
        {"window", "Window", WindowCaptureConfig{
            "program.exe", "WindowClass", "Window title", true}},
        {"camera", "Camera", CameraConfig{"camera-1", QSize(1280, 720), 30}},
        {"image", "Image", ImageConfig{"fixture.png"}, {QSize(64, 32)}},
        {"text", "Text", TextConfig{"Caption", QFont("Arial", 24), Qt::yellow}}};
    const QList<SourceType> types{
        SourceType::DisplayCapture, SourceType::WindowCapture,
        SourceType::Camera, SourceType::Image, SourceType::Text};
    const QStringList typeNames{"displayCapture", "windowCapture", "camera", "image", "text"};
    Manager state(sources);
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    for (int row = 0; row < sources.size(); ++row) {
        QCOMPARE(sources.at(row).type(), types.at(row));
        const auto index = model->index(row, 0);
        QCOMPARE(index.data(Manager::TypeRole).toString(), typeNames.at(row));
        if (types.at(row) == SourceType::Image) {
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
    sources[0].config = ImageConfig{"replacement.png"};
    QCOMPARE(sources.at(0).type(), SourceType::Image);
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

void SourceListModelTest::selectionNotifiesOnlyOnChange()
{
    SourceListModel state(sources());
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
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

void SourceListModelTest::removesSelectedSource_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("first") << 0;
    QTest::newRow("middle") << 1;
    QTest::newRow("last") << 2;
}

void SourceListModelTest::removesSelectedSource()
{
    QFETCH(int, row);
    SourceListModel state(sources());
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QString id = model->index(row, 0).data(SourceListModel::IdRole).toString();
    QVERIFY(state.selectSource(id));
    QPersistentModelIndex survivor(model->index((row + 1) % 3, 0));
    const QString survivorId = survivor.data(SourceListModel::IdRole).toString();
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
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
    QCOMPARE(survivor.data(SourceListModel::IdRole).toString(), survivorId);
}

void SourceListModelTest::removalNotifiesBeforeAndAfterMutation()
{
    SourceListModel state(sources());
    auto *model = &state;
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
    QStringList notifications;
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(model->index(1, 0).data(SourceListModel::IdRole).toString(), QString("b"));
        notifications.append("before");
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(1, 0).data(SourceListModel::IdRole).toString(), QString("c"));
        notifications.append("after");
    });
    QVERIFY(state.removeSource("b"));
    QCOMPARE(notifications, QStringList({"before", "after"}));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
}

void SourceListModelTest::movesSourcesAndPreservesIdentity()
{
    SourceListModel state(sources());
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QVERIFY(state.selectSource("a"));
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(state.canMoveCurrentSourceDown());
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QPersistentModelIndex original(model->index(0, 0));
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(model, &QAbstractItemModel::modelReset);
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
    QSignalSpy available(&state, &SourceListModel::sourceMoveAvailabilityChanged);
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
    QCOMPARE(original.data(SourceListModel::IdRole).toString(), QString("a"));
    QCOMPARE(original.data(SourceListModel::NameRole).toString(), QString("Image"));
    QVERIFY(!original.data(SourceListModel::VisibleRole).toBool());
    QVERIFY(original.data(SourceListModel::LockedRole).toBool());
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

void SourceListModelTest::flagsNotifyOnlyChangedRole()
{
    SourceListModel state(sources());
    auto *model = &state;
    QVERIFY(state.selectSource("a"));
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(2).value<QList<int>>(), QList<int>{SourceListModel::VisibleRole});
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.last().at(2).value<QList<int>>(), QList<int>{SourceListModel::LockedRole});
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(!model->index(1, 0).data(SourceListModel::VisibleRole).toBool());
    QVERIFY(model->index(1, 0).data(SourceListModel::LockedRole).toBool());
    QVERIFY(state.removeSource("b")); // Locking does not prevent list removal.
}

void SourceListModelTest::rejectsInvalidOperations()
{
    SourceListModel state(sources());
    QVERIFY(state.selectSource("a"));
    auto *model = &state;
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
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
    SourceListModel empty;
    QVERIFY(!empty.removeSource("a"));
    QVERIFY(!empty.moveSource("a", 1));
}

void SourceListModelTest::suggestsUnusedSourceNames()
{
    const QString base = QString::fromUtf8("图像");
    SourceListModel state(QList<SourceData>{
        {"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}});
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &SourceListModel::currentSourceIdChanged);
    QSignalSpy inserted(&state, &QAbstractItemModel::rowsInserted);
    QCOMPARE(state.suggestedSourceName(base), base + " 2");
    QCOMPARE(state.suggestedSourceName(" camera "), QString("camera 3"));
    QVERIFY(state.suggestedSourceName({}).isEmpty());
    QVERIFY(state.suggestedSourceName(" \t ").isEmpty());
    QCOMPARE(state.rowCount(), 4);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QVERIFY(state.removeSource("a"));
    QCOMPARE(state.suggestedSourceName(base), base);
}

void SourceListModelTest::sameImageEntriesRemainIndependent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("shared.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    QList<SourceData> entries{
        {"a", "First", ImageConfig{path}, {image.size()}},
        {"b", "Second", ImageConfig{path}, {image.size()}}};
    SourceListModel state(entries);
    entries.clear(); // State owns its source list.
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto other = state.index(0, 0);
    QCOMPARE(other.data(SourceListModel::IdRole).toString(), QString("b"));
    QCOMPARE(other.data(SourceListModel::OriginalSizeRole).toSize(), image.size());
    QVERIFY(other.data(SourceListModel::VisibleRole).toBool());
    QVERIFY(!other.data(SourceListModel::LockedRole).toBool());
    QVERIFY(state.selectSource("a"));
    QVERIFY(state.removeSource("a"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.rowCount(), 1);
    QCOMPARE(QImage(path), image);
    QVERIFY(state.removeSource("b"));
    QCOMPARE(state.rowCount(), 0);
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
}

QTEST_GUILESS_MAIN(SourceListModelTest)
#include "tst_sourcelistmodel.moc"

void SourceListModelTest::insertsSourcesAtTop()
{
    SourceListModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy availability(&model, &SourceListModel::sourceMoveAvailabilityChanged);
    QVERIFY(!model.addSource({" ", "Invalid"}));
    QVERIFY(model.addSource({"one", "First", ImageConfig{"one.png"}}));
    QVERIFY(!model.addSource({"one", "Duplicate"}));
    QVERIFY(model.addSource({"two", "Second", ImageConfig{"one.png"}}));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.currentSourceId(), QString("two"));
    QCOMPARE(model.index(0, 0).data(SourceListModel::IdRole).toString(), QString("two"));
    QVERIFY(model.canMoveCurrentSourceDown());
    QVERIFY(!model.canMoveCurrentSourceUp());
    QVERIFY(!availability.isEmpty());
    QVERIFY(model.setSourceOriginalSize("two", QSize(40, 20)));
    QVERIFY(model.setSourceOriginalSize("two", QSize(40, 20)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!model.setSourceOriginalSize("missing", QSize(40, 20)));
}
