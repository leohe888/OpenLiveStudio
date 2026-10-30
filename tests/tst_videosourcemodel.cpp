#include "videosourcemodel.h"

#include <QAbstractItemModelTester>
#include <limits>
#include <QFile>
#include <QImage>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

class VideoSourceModelTest : public QObject
{
    Q_OBJECT
private slots:
    void insertsSourcesAtTop();
    void displayPropertiesPreserveSourceState();
    void initializesAndValidatesTransforms();
    void sharesGeometryForMappingAndResizing();
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
    void filtersAreOrderedValidatedAndIndependent();
};

static QList<SourceData> sources()
{
    return {{"a", "Image"}, {"b", "Camera"}, {"c", "Text"}};
}

void VideoSourceModelTest::startsWithEmptySourceList()
{
    VideoSourceModel state;
    QAbstractItemModelTester tester(&state, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(state.rowCount(), 0);
    QVERIFY(state.metaObject()->indexOfProperty("videoSourceModel") < 0);
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

void VideoSourceModelTest::exposesSourceRolesAndRejectsInvalidIndexes()
{
    VideoSourceModel state(QList<SourceData>{
        {"image", "Logo", ImageConfig{"fixture.png"}, {QSize(64, 32)}, false, true}});
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QHash<int, QByteArray> roles{
        {VideoSourceModel::IdRole, "sourceId"}, {VideoSourceModel::NameRole, "sourceName"},
        {VideoSourceModel::TypeRole, "sourceType"}, {VideoSourceModel::FilePathRole, "sourceFilePath"},
        {VideoSourceModel::OriginalSizeRole, "sourceOriginalSize"},
        {VideoSourceModel::VisibleRole, "sourceVisible"}, {VideoSourceModel::LockedRole, "sourceLocked"},
        {VideoSourceModel::TransformRole, "sourceTransform"}, {VideoSourceModel::FiltersRole, "sourceFilters"},
        {VideoSourceModel::DisplayConfigRole, "sourceDisplayConfig"},
        {VideoSourceModel::CameraConfigRole, "sourceCameraConfig"},
        {VideoSourceModel::CaptureStateRole, "sourceCaptureState"}, {VideoSourceModel::CaptureErrorRole, "sourceCaptureError"}};
    QCOMPARE(model->roleNames(), roles);
    const auto index = model->index(0, 0);
    QCOMPARE(index.data(VideoSourceModel::IdRole).toString(), QString("image"));
    QCOMPARE(index.data(Qt::DisplayRole).toString(), QString("Logo"));
    QCOMPARE(index.data(VideoSourceModel::FilePathRole).toString(), QString("fixture.png"));
    QCOMPARE(index.data(VideoSourceModel::OriginalSizeRole).toSize(), QSize(64, 32));
    QVERIFY(!index.data(VideoSourceModel::VisibleRole).toBool());
    QVERIFY(index.data(VideoSourceModel::LockedRole).toBool());
    QCOMPARE(model->rowCount(index), 0);
    QVERIFY(!model->data({}, VideoSourceModel::IdRole).isValid());
    QVERIFY(!model->index(1, 0).isValid());
    QVERIFY(!model->index(0, 1).isValid());
    QVERIFY(!index.data(Qt::UserRole + 100).isValid());
    VideoSourceModel other(sources());
    QVERIFY(!model->data(other.index(0, 0), VideoSourceModel::IdRole).isValid());
}

void VideoSourceModelTest::supportsAllSourceConfigurations()
{
    using Manager = VideoSourceModel;
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

void VideoSourceModelTest::selectionNotifiesOnlyOnChange()
{
    VideoSourceModel state(sources());
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
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

void VideoSourceModelTest::removesSelectedSource_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("first") << 0;
    QTest::newRow("middle") << 1;
    QTest::newRow("last") << 2;
}

void VideoSourceModelTest::removesSelectedSource()
{
    QFETCH(int, row);
    VideoSourceModel state(sources());
    auto *model = &state;
    QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const QString id = model->index(row, 0).data(VideoSourceModel::IdRole).toString();
    QVERIFY(state.selectSource(id));
    QPersistentModelIndex survivor(model->index((row + 1) % 3, 0));
    const QString survivorId = survivor.data(VideoSourceModel::IdRole).toString();
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
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
    QCOMPARE(survivor.data(VideoSourceModel::IdRole).toString(), survivorId);
}

void VideoSourceModelTest::removalNotifiesBeforeAndAfterMutation()
{
    VideoSourceModel state(sources());
    auto *model = &state;
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
    QStringList notifications;
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(model->index(1, 0).data(VideoSourceModel::IdRole).toString(), QString("b"));
        notifications.append("before");
    });
    connect(model, &QAbstractItemModel::rowsRemoved, this, [&] {
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->index(1, 0).data(VideoSourceModel::IdRole).toString(), QString("c"));
        notifications.append("after");
    });
    QVERIFY(state.removeSource("b"));
    QCOMPARE(notifications, QStringList({"before", "after"}));
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
}

void VideoSourceModelTest::movesSourcesAndPreservesIdentity()
{
    VideoSourceModel state(sources());
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
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
    QSignalSpy available(&state, &VideoSourceModel::sourceMoveAvailabilityChanged);
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
    QCOMPARE(original.data(VideoSourceModel::IdRole).toString(), QString("a"));
    QCOMPARE(original.data(VideoSourceModel::NameRole).toString(), QString("Image"));
    QVERIFY(!original.data(VideoSourceModel::VisibleRole).toBool());
    QVERIFY(original.data(VideoSourceModel::LockedRole).toBool());
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

void VideoSourceModelTest::flagsNotifyOnlyChangedRole()
{
    VideoSourceModel state(sources());
    auto *model = &state;
    QVERIFY(state.selectSource("a"));
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(changed.first().at(2).value<QList<int>>(), QList<int>{VideoSourceModel::VisibleRole});
    QVERIFY(state.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.last().at(2).value<QList<int>>(), QList<int>{VideoSourceModel::LockedRole});
    QVERIFY(state.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QVERIFY(!model->index(1, 0).data(VideoSourceModel::VisibleRole).toBool());
    QVERIFY(model->index(1, 0).data(VideoSourceModel::LockedRole).toBool());
    QVERIFY(state.removeSource("b")); // Locking does not prevent list removal.
}

void VideoSourceModelTest::rejectsInvalidOperations()
{
    VideoSourceModel state(sources());
    QVERIFY(state.selectSource("a"));
    auto *model = &state;
    QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
    QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
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
    VideoSourceModel empty;
    QVERIFY(!empty.removeSource("a"));
    QVERIFY(!empty.moveSource("a", 1));
}

void VideoSourceModelTest::suggestsUnusedSourceNames()
{
    const QString base = QString::fromUtf8("图像");
    VideoSourceModel state(QList<SourceData>{
        {"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}});
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &VideoSourceModel::currentSourceIdChanged);
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

void VideoSourceModelTest::sameImageEntriesRemainIndependent()
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
    VideoSourceModel state(entries);
    entries.clear(); // State owns its source list.
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto other = state.index(0, 0);
    QCOMPARE(other.data(VideoSourceModel::IdRole).toString(), QString("b"));
    QCOMPARE(other.data(VideoSourceModel::OriginalSizeRole).toSize(), image.size());
    QVERIFY(other.data(VideoSourceModel::VisibleRole).toBool());
    QVERIFY(!other.data(VideoSourceModel::LockedRole).toBool());
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

QTEST_GUILESS_MAIN(VideoSourceModelTest)
#include "tst_videosourcemodel.moc"

void VideoSourceModelTest::insertsSourcesAtTop()
{
    VideoSourceModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy availability(&model, &VideoSourceModel::sourceMoveAvailabilityChanged);
    QVERIFY(!model.addSource({" ", "Invalid"}));
    QVERIFY(model.addSource({"one", "First", ImageConfig{"one.png"}}));
    QVERIFY(!model.addSource({"one", "Duplicate"}));
    QVERIFY(model.addSource({"two", "Second", ImageConfig{"one.png"}}));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.currentSourceId(), QString("two"));
    QCOMPARE(model.index(0, 0).data(VideoSourceModel::IdRole).toString(), QString("two"));
    QVERIFY(model.canMoveCurrentSourceDown());
    QVERIFY(!model.canMoveCurrentSourceUp());
    QVERIFY(!availability.isEmpty());
    QVERIFY(model.setSourceOriginalSize("two", QSize(40, 20)));
    QVERIFY(model.setSourceOriginalSize("two", QSize(40, 20)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!model.setSourceOriginalSize("missing", QSize(40, 20)));
}

void VideoSourceModelTest::initializesAndValidatesTransforms()
{
    VideoSourceModel model;
    QVERIFY(model.addSource({"image", "Image", ImageConfig{"image.png"}}));
    const auto current = [&] { return model.index(0, 0).data(VideoSourceModel::TransformRole).value<SourceTransform>(); };
    QVERIFY(current().size.isEmpty());
    QVERIFY(model.setSourceOriginalSize("image", QSize(4000, 2000)));
    QCOMPARE(current().center, QPointF(960, 540));
    QCOMPARE(current().size, QSizeF(1920, 960));
    SourceTransform edited{{-20.5, 95.25}, {300.5, 100.25}, -30};
    QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(current().rotation, 330.0);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.last()[2].value<QList<int>>(), QList<int>{VideoSourceModel::TransformRole});
    edited.rotation = 690;
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(changes.size(), 1);
    QVERIFY(model.setSourceOriginalSize("image", QSize(600, 200)));
    QCOMPARE(current().center, edited.center);
    QCOMPARE(current().size, edited.size);
    const SourceTransform before = current();
    QVERIFY(!model.setSourceTransform("missing", before));
    QVERIFY(model.setSourceLocked("image", true));
    edited.center += QPointF(20, 20);
    QVERIFY(!model.setSourceTransform("image", edited));
    QVERIFY(model.setSourceLocked("image", false));
    for (int part = 0; part < 5; ++part) {
        auto invalid = before;
        if (part == 0) invalid.center.setX(std::numeric_limits<qreal>::infinity());
        if (part == 1) invalid.center.setY(std::numeric_limits<qreal>::quiet_NaN());
        if (part == 2) invalid.size.setWidth(.5);
        if (part == 3) invalid.size.setHeight(-1);
        if (part == 4) invalid.rotation = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!model.setSourceTransform("image", invalid));
        QVERIFY(current() == before);
    }
    VideoSourceModel ready({{"ready", "Ready", ImageConfig{"ready.png"}, {QSize(40, 20)}}});
    QCOMPARE(ready.index(0, 0).data(VideoSourceModel::TransformRole).value<SourceTransform>().size, QSizeF(40, 20));
}

void VideoSourceModelTest::sharesGeometryForMappingAndResizing()
{
    const auto thin = SourceGeometry::initial(QSizeF(1, 4000));
    QVERIFY(SourceGeometry::valid(thin));
    QCOMPARE(thin.size.height(), 1080.0);
    QCOMPARE(thin.size.width(), .27);
    for (const QSizeF &widget : {QSizeF(1000, 700), QSizeF(300, 900), QSizeF(1600, 400)}) {
        const auto display = SourceGeometry::canvasToWidget(widget);
        const QPointF point(135.25, 980.75);
        const auto roundTrip = display.inverted().map(display.map(point));
        QVERIFY(std::hypot(roundTrip.x() - point.x(), roundTrip.y() - point.y()) < 1e-8);
        QVERIFY(SourceGeometry::canvasRect(widget).contains(display.map(point)));
    }
    for (qreal angle : {0., 37., 90., 225.}) {
        const SourceTransform start{{960, 540}, {400, 200}, angle};
        const auto matrix = SourceGeometry::sourceToCanvas(start);
        QVERIFY(SourceGeometry::contains(start, matrix.map(QPointF(200, 100))));
        QVERIFY(SourceGeometry::contains(start, matrix.map(QPointF(10, 10))));
        QVERIFY(!SourceGeometry::contains(start, matrix.map(QPointF(-10, 100))));
        QTransform rotation;
        rotation.rotate(angle);
        for (int handle = 0; handle < 8; ++handle) {
            const auto result = SourceGeometry::resized(start, handle, rotation.map(QPointF(40, 30)));
            QVERIFY(SourceGeometry::valid(result));
            const auto fraction = SourceGeometry::handleFractions[handle];
            const QPointF opposite(1 - fraction.x(), 1 - fraction.y());
            const auto anchor = matrix.map(QPointF(opposite.x() * 400, opposite.y() * 200));
            const auto after = SourceGeometry::sourceToCanvas(result).map(
                QPointF(opposite.x() * result.size.width(), opposite.y() * result.size.height()));
            QVERIFY(std::hypot(anchor.x() - after.x(), anchor.y() - after.y()) < 1e-8);
            if (handle % 2 == 0)
                QVERIFY(qAbs(result.size.width() / result.size.height() - 2) < 1e-8);
            else if (fraction.x() == .5)
                QCOMPARE(result.size.width(), 400.0);
            else
                QCOMPARE(result.size.height(), 200.0);
        }
        const auto crossed = SourceGeometry::resized(start, 0, rotation.map(QPointF(10000, 10000)));
        QVERIFY(crossed.size.width() >= 1 && crossed.size.height() >= 1);
        const auto narrow = SourceGeometry::resized(start, 3, rotation.map(QPointF(-10000, 0)));
        QCOMPARE(narrow.size.width(), 1.0);
    }
}

void VideoSourceModelTest::filtersAreOrderedValidatedAndIndependent()
{
    VideoSourceModel model(QList<SourceData>{{"a", "A", ImageConfig{"a.png"}},
        {"b", "B", ImageConfig{"a.png"}}, {"camera", "Camera", CameraConfig{}}});
    QSignalSpy changed(&model, &VideoSourceModel::sourceFiltersChanged);
    QSignalSpy data(&model, &VideoSourceModel::dataChanged);
    QVERIFY(!model.canFilterCurrentSource());
    QVERIFY(model.selectSource("a"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(model.canFilterSource("camera"));
    QVERIFY(model.addColorCorrection("missing").isEmpty());
    const QString first = model.addColorCorrection("a");
    const QString second = model.addColorCorrection("a");
    QVERIFY(!first.isEmpty() && first != second);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(data.last()[2].value<QList<int>>(), QList<int>{VideoSourceModel::FiltersRole});
    QCOMPARE(model.sourceFilters("a")[0].toMap()["brightness"].toFloat(), 0.f);
    QCOMPARE(model.sourceFilters("a")[0].toMap()["contrast"].toFloat(), 1.f);
    QCOMPARE(model.sourceFilters("a")[0].toMap()["saturation"].toFloat(), 1.f);
    QVERIFY(model.sourceFilters("b").isEmpty());
    QVERIFY(model.setSourceLocked("a", true));
    QVERIFY(model.setFilterParameters("a", first, .2, 1.5, 0));
    QVERIFY(model.setFilterEnabled("a", first, false));
    const int before = changed.count();
    QVERIFY(model.setFilterEnabled("a", first, false));
    QVERIFY(model.setFilterParameters("a", first, .2, 1.5, 0));
    QCOMPARE(changed.count(), before);
    for (double invalid : {-1.01, 1.01, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
        QVERIFY(!model.setFilterParameters("a", first, invalid, 1, 1));
    for (double invalid : {-.01, 2.01, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        QVERIFY(!model.setFilterParameters("a", first, 0, invalid, 1));
        QVERIFY(!model.setFilterParameters("a", first, 0, 1, invalid));
    }
    QVERIFY(!model.moveFilter("a", first, -1));
    QVERIFY(!model.moveFilter("a", second, 1));
    QVERIFY(!model.moveFilter("a", first, 2));
    QVERIFY(model.moveFilter("a", first, 1));
    QCOMPARE(model.sourceFilters("a")[1].toMap()["id"].toString(), first);
    QVERIFY(model.setFilterEnabled("a", first, true));
    QCOMPARE(model.sourceFilters("a")[1].toMap()["brightness"].toFloat(), .2f);
    const auto typed = model.index(0, 0).data(VideoSourceModel::FiltersRole).value<QList<SourceFilter>>();
    QCOMPARE(typed[1].id, first);
    QCOMPARE(typed[1].saturation, 0.f);
    QVERIFY(!model.setFilterEnabled("b", first, false));
    QVERIFY(!model.setFilterParameters("missing", first, 0, 1, 1));
    QVERIFY(!model.moveFilter("missing", first, 1));
    QVERIFY(!model.removeFilter("a", "missing"));
    QVERIFY(model.removeFilter("a", first));
    QCOMPARE(model.sourceFilters("a").size(), 1);
    QVERIFY(model.removeSource("a"));
    QVERIFY(model.sourceFilters("a").isEmpty());
    QVERIFY(!model.removeFilter("a", second));
}

void VideoSourceModelTest::displayPropertiesPreserveSourceState()
{
    VideoSourceModel model(QList<SourceData>{{"screen", "Screen", DisplayCaptureConfig{"physical-path", true}},
        {"image", "Image", ImageConfig{"a.png"}}, {"camera", "Camera", CameraConfig{}}});
    QSignalSpy changed(&model, &VideoSourceModel::dataChanged);
    QSignalSpy status(&model, &VideoSourceModel::captureStatusChanged);
    QVERIFY(model.selectSource("screen"));
    QVERIFY(model.canEditCurrentDisplay() && model.canFilterCurrentSource());
    QVERIFY(model.setSourceOriginalSize("screen", QSize(1920, 1080)));
    const SourceTransform transform{{300, 500}, {640, 360}, 45};
    QVERIFY(model.setSourceTransform("screen", transform));
    const auto filter = model.addColorCorrection("screen");
    QVERIFY(model.setSourceLocked("screen", true));
    const int before = changed.count();
    QVERIFY(!model.setDisplayConfig("screen", " ", true));
    QVERIFY(!model.setDisplayConfig("image", "another", true));
    QVERIFY(!model.setDisplayConfig("missing", "another", true));
    QVERIFY(model.setDisplayConfig("screen", "physical-path", true));
    QCOMPARE(changed.count(), before);
    QVERIFY(model.setDisplayConfig("screen", "another-physical-path", false));
    QCOMPARE(changed.last()[2].value<QList<int>>(), QList<int>{VideoSourceModel::DisplayConfigRole});
    QCOMPARE(model.index(0, 0).data(VideoSourceModel::TransformRole).value<SourceTransform>(), transform);
    QCOMPARE(model.sourceFilters("screen")[0].toMap()["id"].toString(), filter);
    QCOMPARE(model.displayProperties("screen").value("displayId").toString(), QString("another-physical-path"));
    QVERIFY(!model.displayProperties("screen").value("captureCursor").toBool());
    QVERIFY(model.setSourceCaptureStatus("screen", "error", "Disconnected"));
    QCOMPARE(status.count(), 1);
    QVERIFY(model.setSourceCaptureStatus("screen", "error", "Disconnected"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(model.index(0, 0).data(VideoSourceModel::CaptureStateRole).toString(), QString("error"));
    QCOMPARE(model.displayProperties("screen").value("error").toString(), QString("Disconnected"));
    QVERIFY(model.setSourceCaptureStatus("screen", "starting", {}));
    QVERIFY(model.displayProperties("screen").value("error").toString().isEmpty());
    QVERIFY(model.selectSource("image"));
    QVERIFY(!model.canEditCurrentDisplay() && model.canFilterCurrentSource());
    QVERIFY(model.selectSource("camera"));
    QVERIFY(!model.canEditCurrentDisplay() && model.canFilterCurrentSource() && model.canEditCurrentSource());
    QVERIFY(model.removeSource("screen"));
    QVERIFY(model.displayProperties("screen").isEmpty());
    QVERIFY(!model.setSourceCaptureStatus("screen", "error", "Removed"));
}
