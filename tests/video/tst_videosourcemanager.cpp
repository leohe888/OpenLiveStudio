#include "video/videosourcemanager.h"
#include "videosourcetesthelpers.h"

#include <limits>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QTest>

class VideoSourceManagerTest : public QObject
{
    Q_OBJECT
private slots:
    void filterSelectionFollowsSourceAndFilterLifetime();
    void filterMoveAvailability();
    void topLevelProperties();
    void insertsSourcesAtTop();
    void sourceRemovalDuringGeometryNotificationIsSafe();
    void sourceReorderingDuringGeometryNotificationIsSafe();
    void captureSettingUpdatesPreserveSourceState();
    void initializesAndValidatesTransform();
    void mapsSourceCoordinates();
    void startsWithEmptySourceList();
    void exposesTypedSourcesAndRejectsMissingIds();
    void supportsAllSourceTypes();
    void ownsSourcesAndKeepsPointersStable();
    void selectionCallbacksObserveRemovedSource();
    void cameraCaptureNotificationsIncludeMetadata();
    void statusDetailsFollowSourceType();
    void selectionNotifiesOnlyOnChange();
    void removesSelectedSource_data();
    void removesSelectedSource();
    void removalNotifiesAfterMutation();
    void movesSourcesAndPreservesIdentity();
    void flagsNotifyOnlyChangedSource();
    void rejectsInvalidOperations();
    void suggestsUnusedSourceNames();
    void sameImageEntriesRemainIndependent();
    void filtersAreOrderedValidatedAndIndependent();
};

static const VideoSource *findSource(const VideoSourceManager &manager, const QString &sourceId)
{
    return manager.source(sourceId);
}

static QList<SourceFixture> sources()
{
    return {{"a", "Image"}, {"b", "Camera"}, {"c", "Display"}};
}

void VideoSourceManagerTest::startsWithEmptySourceList()
{
    VideoSourceManager state;
    QCOMPARE(state.sources().size(), 0);
    QVERIFY(state.metaObject()->indexOfProperty("videoSourceModel") < 0);
    QVERIFY(state.currentSourceId().isEmpty());
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QCOMPARE(state.suggestedSourceName("Camera"), QString("Camera"));
    QVERIFY(!state.selectSource("missing"));
    QVERIFY(state.selectSource({}));
    QVERIFY(state.metaObject()->indexOfProperty("sceneModel") < 0);
    for (const auto *method :
         {"selectScene(QString)", "addScene(QString)", "renameScene(QString,QString)",
          "removeScene(QString)", "moveScene(QString,int)"})
        QVERIFY(state.metaObject()->indexOfMethod(method) < 0);
}

void VideoSourceManagerTest::exposesTypedSourcesAndRejectsMissingIds()
{
    VideoSourceManager manager;
    QVERIFY(addSources(manager, {{"image", "Logo", false, true, {}, {}, {}, "fixture.png"}}));
    QCOMPARE(manager.sources().size(), 1);
    QCOMPARE(manager.sourceName("image"), QString("Logo"));
    QVERIFY(manager.sourceName("missing").isNull());
    QVERIFY(manager.sourceName({}).isNull());
    const auto *source = findSource(manager, "image");
    QVERIFY(source);
    QCOMPARE(source->id(), QString("image"));
    QCOMPARE(source->name(), QString("Logo"));
    QVERIFY(setSourcePixelSize(manager, "image", {64, 32}));
    QCOMPARE(manager.sourcePixelSize("image"), QSize(64, 32));
    QCOMPARE(findSource(manager, "image")->pixelSize(), QSize(64, 32));
    QVERIFY(!source->visible());
    QVERIFY(source->locked());
    QCOMPARE(dynamic_cast<const ImageSource *>(source)->filePath(), QString("fixture.png"));
    QVERIFY(!findSource(manager, "missing"));
    QVERIFY(!cameraSource(manager, "image"));
    QVERIFY(!displaySource(manager, "image"));
    QVERIFY(!cameraSource(manager, "missing"));
    QVERIFY(!displaySource(manager, "missing"));
    QVERIFY(!findSource(manager, {}));
    QVERIFY(addSource(manager, {"unnamed", {}}));
    QVERIFY(manager.sourceName("unnamed").isEmpty());
    QVERIFY(!manager.sourceName("unnamed").isNull());
}

void VideoSourceManagerTest::supportsAllSourceTypes()
{
    QList<SourceFixture> entries{
        {"display", "Display", false},
        {"camera", "Camera", cameraRequest("camera-1", QSize(1280, 720), 30, 1)},
        {"image", "Image", "fixture.png"}};
    VideoSourceManager manager;
    QVERIFY(addSources(manager, entries));
    setSourcePixelSize(manager, "image", {64, 32});
    QCOMPARE(findSource(manager, "display")->type(), VideoSourceType::DisplayCapture);
    QCOMPARE(findSource(manager, "camera")->type(), VideoSourceType::Camera);
    QCOMPARE(findSource(manager, "image")->type(), VideoSourceType::Image);
    QCOMPARE(manager.sourcePixelSize("image"), QSize(64, 32));
    for (const auto &entry : entries) {
        QVERIFY(manager.selectSource(entry.id()));
        QVERIFY(manager.setSourceVisible(entry.id(), false));
        QVERIFY(manager.setSourceLocked(entry.id(), true));
    }
    entries[0] = SourceFixture{"display", "Replacement", "replacement.png"};
    QCOMPARE(findSource(manager, "display")->type(), VideoSourceType::DisplayCapture);
    QVERIFY(manager.moveSource("image", -1));
    QCOMPARE(manager.sources().at(1)->id(), QString("image"));
    for (const auto &source : manager.sources()) {
        QVERIFY(!source->visible());
        QVERIFY(source->locked());
    }
    QVERIFY(manager.removeSource("image"));
    QCOMPARE(manager.sources().at(1)->type(), VideoSourceType::Camera);
}

void VideoSourceManagerTest::statusDetailsFollowSourceType()
{
    CameraSource camera{"camera", "Camera", "device"};
    ImageSource image{"image", "Image", "image.png"};
    VideoSourceManager manager;
    QVERIFY(addSources(manager, {camera, image}));
    QVERIFY(setCameraActualMode(manager, "camera", {1280, 720}, 30000, 1001));
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateNumerator(), 30000);
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateDenominator(), 1001);
    QVERIFY(!setCameraActualMode(manager, "image", {800, 600}, 30, 1));
    QVERIFY(!setSourceStatus(manager, "missing", VideoSourceState::Ready, {}));
    QVERIFY(!setSourcePixelSize(manager, "missing", {64, 32}));
    QSignalSpy changed(&manager, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&manager, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Error, "Load failed"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.last().at(0).toString(), QString("image"));
    QCOMPARE(manager.sourceError("image"), QString("Load failed"));
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Error, "Load failed"));
    QCOMPARE(status.count(), 1);
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Ready, {}));
    QVERIFY(manager.sourceError("image").isEmpty());
    QCOMPARE(changed.count(), 0);
    // 验证运行状态随来源快照复制，且修改副本不影响原数据。
    VideoSourceManager copy;
    QVERIFY(addSources(copy, manager.sources()));
    QCOMPARE(copy.sourcePixelSize("camera"), QSize(1280, 720));
    QCOMPARE(cameraSource(copy, "camera")->negotiatedFrameRateNumerator(), 30000);
    QCOMPARE(cameraSource(copy, "camera")->negotiatedFrameRateDenominator(), 1001);
    QCOMPARE(findSource(copy, "image")->state(), VideoSourceState::Ready);
    QVERIFY(setSourceStatus(copy, "image", VideoSourceState::Error, "Copy failed"));
    QCOMPARE(findSource(manager, "image")->state(), VideoSourceState::Ready);
    QVERIFY(findSource(manager, "image")->error().isEmpty());
    QVERIFY(manager.moveSource("camera", 1));
    QCOMPARE(manager.sourcePixelSize("camera"), QSize(1280, 720));
    QVERIFY(manager.removeSource("camera"));
    QVERIFY(manager.sourcePixelSize("camera").isEmpty());
    QVERIFY(addSource(manager, camera));
    QVERIFY(manager.sourceState("camera") == VideoSourceState::Idle);
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateNumerator(), 0);
    QVERIFY(manager.sourceState("image") == VideoSourceState::Ready);
    camera.updateCapture({640, 360}, 30, 1, {});
    QVERIFY(manager.removeSource("camera"));
    QVERIFY(addSource(manager, camera));
    QCOMPARE(findSource(manager, "camera")->pixelSize(), QSize(640, 360));
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateNumerator(), 30);
    QCOMPARE(findSource(manager, "camera")->transform().size(), QSizeF(640, 360));
    VideoSourceManager initialized;
    QVERIFY(addSources(initialized, {camera}));
    QCOMPARE(findSource(initialized, "camera")->state(), VideoSourceState::Capturing);
    QCOMPARE(findSource(initialized, "camera")->transform().size(), QSizeF(640, 360));
}

void VideoSourceManagerTest::selectionNotifiesOnlyOnChange()
{
    VideoSourceManager state;
    QVERIFY(addSources(state, sources()));
    QSignalSpy selected(&state, &VideoSourceManager::currentSourceIdChanged);
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

void VideoSourceManagerTest::removesSelectedSource_data()
{
    QTest::addColumn<int>("row");
    QTest::newRow("first") << 0;
    QTest::newRow("middle") << 1;
    QTest::newRow("last") << 2;
}

void VideoSourceManagerTest::removesSelectedSource()
{
    QFETCH(int, row);
    VideoSourceManager manager;
    QVERIFY(addSources(manager, sources()));
    const QString id = manager.sources().at(row)->id();
    const QString survivorId = manager.sources().at((row + 1) % 3)->id();
    QVERIFY(manager.selectSource(id));
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    QSignalSpy changed(&manager, &VideoSourceManager::sourcesChanged);
    QVERIFY(manager.removeSource(id));
    QCOMPARE(manager.sources().size(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(selected.count(), 1);
    QVERIFY(manager.currentSourceId().isEmpty());
    QVERIFY(!findSource(manager, id));
    QVERIFY(findSource(manager, survivorId));
}

void VideoSourceManagerTest::removalNotifiesAfterMutation()
{
    VideoSourceManager manager;
    QVERIFY(addSources(manager, sources()));
    QVERIFY(manager.selectSource("a"));
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    int notifications = 0;
    connect(&manager, &VideoSourceManager::sourcesChanged, this, [&] {
        QCOMPARE(manager.sources().size(), 2);
        QCOMPARE(manager.sources().at(1)->id(), QString("c"));
        QVERIFY(!findSource(manager, "b"));
        ++notifications;
    });
    QVERIFY(manager.removeSource("b"));
    QCOMPARE(notifications, 1);
    QCOMPARE(manager.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
}

void VideoSourceManagerTest::movesSourcesAndPreservesIdentity()
{
    VideoSourceManager manager;
    QVERIFY(addSources(manager, sources()));
    QVERIFY(manager.selectSource("a"));
    QVERIFY(!manager.canMoveCurrentSourceUp());
    QVERIFY(manager.canMoveCurrentSourceDown());
    QVERIFY(manager.setSourceVisible("a", false));
    QVERIFY(manager.setSourceLocked("a", true));
    QSignalSpy changed(&manager, &VideoSourceManager::sourcesChanged);
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    QSignalSpy available(&manager, &VideoSourceManager::currentSourceMoveAvailabilityChanged);
    QVERIFY(manager.moveSource("a", 1));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(available.count(), 1);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(manager.sources().at(1)->id(), QString("a"));
    QCOMPARE(findSource(manager, "a")->name(), QString("Image"));
    QVERIFY(!findSource(manager, "a")->visible());
    QVERIFY(findSource(manager, "a")->locked());
    QVERIFY(manager.canMoveCurrentSourceUp());
    QVERIFY(manager.canMoveCurrentSourceDown());
    QVERIFY(manager.removeSource("c"));
    QVERIFY(!manager.canMoveCurrentSourceDown());
    QVERIFY(manager.removeSource("b"));
    QVERIFY(!manager.canMoveCurrentSourceUp());
    QCOMPARE(available.count(), 3);
    QCOMPARE(manager.currentSourceId(), QString("a"));
}

void VideoSourceManagerTest::flagsNotifyOnlyChangedSource()
{
    VideoSourceManager manager;
    QVERIFY(addSources(manager, sources()));
    QVERIFY(manager.selectSource("a"));
    QSignalSpy changed(&manager, &VideoSourceManager::sourceChanged);
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    QVERIFY(manager.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toString(), QString("b"));
    QVERIFY(manager.setSourceVisible("b", false));
    QCOMPARE(changed.count(), 1);
    QVERIFY(manager.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QVERIFY(manager.setSourceLocked("b", true));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(selected.count(), 0);
    QVERIFY(!findSource(manager, "b")->visible());
    QVERIFY(findSource(manager, "b")->locked());
    QVERIFY(manager.removeSource("b"));
}

void VideoSourceManagerTest::rejectsInvalidOperations()
{
    VideoSourceManager state;
    QVERIFY(addSources(state, sources()));
    QVERIFY(state.selectSource("a"));
    auto *model = &state;
    QSignalSpy removed(model, &VideoSourceManager::sourcesChanged);
    QSignalSpy moved(model, &VideoSourceManager::sourcesChanged);
    QSignalSpy changed(model, &VideoSourceManager::sourceChanged);
    QSignalSpy selected(&state, &VideoSourceManager::currentSourceIdChanged);
    for (const auto &id : {QString(), QString("missing")}) {
        QVERIFY(!state.removeSource(id));
        QVERIFY(!state.moveSource(id, 1));
        QVERIFY(!state.setSourceVisible(id, false));
        QVERIFY(!state.setSourceLocked(id, true));
    }
    for (int offset : {-2, 0, 2})
        QVERIFY(!state.moveSource("a", offset));
    QVERIFY(!state.moveSource("a", -1));
    QVERIFY(!state.moveSource("c", 1));
    QCOMPARE(removed.count(), 0);
    QCOMPARE(moved.count(), 0);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(model->sources().size(), 3);
    VideoSourceManager empty;
    QVERIFY(!empty.removeSource("a"));
    QVERIFY(!empty.moveSource("a", 1));
}

void VideoSourceManagerTest::suggestsUnusedSourceNames()
{
    const QString base = QString::fromUtf8("鍥惧儚");
    VideoSourceManager state;
    QVERIFY(addSources(state,
                       QList<SourceFixture>{
                           {"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}}));
    QVERIFY(state.selectSource("a"));
    QSignalSpy selected(&state, &VideoSourceManager::currentSourceIdChanged);
    QSignalSpy inserted(&state, &VideoSourceManager::sourcesChanged);
    QCOMPARE(state.suggestedSourceName(base), base + " 2");
    QCOMPARE(state.suggestedSourceName(" camera "), QString("camera 3"));
    QVERIFY(state.suggestedSourceName({}).isEmpty());
    QVERIFY(state.suggestedSourceName(" \t ").isEmpty());
    QCOMPARE(state.sources().size(), 4);
    QCOMPARE(state.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
    QCOMPARE(inserted.count(), 0);
    QVERIFY(state.removeSource("a"));
    QCOMPARE(state.suggestedSourceName(base), base);
}

void VideoSourceManagerTest::sameImageEntriesRemainIndependent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("shared.png");
    QImage image(19, 11, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(path));
    QList<SourceFixture> entries{{"a", "First", path}, {"b", "Second", path}};
    VideoSourceManager state;
    QVERIFY(addSources(state, entries));
    setSourcePixelSize(state, "a", image.size());
    setSourcePixelSize(state, "b", image.size());
    entries.clear(); // 清空输入容器后仍应保留来源，验证管理器持有独立副本。
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto *other = findSource(state, "b");
    QCOMPARE(other->id(), QString("b"));
    QCOMPARE(state.sourcePixelSize(other->id()), image.size());
    QVERIFY(other->visible());
    QVERIFY(!other->locked());
    QVERIFY(state.selectSource("a"));
    QVERIFY(state.removeSource("a"));
    QVERIFY(state.currentSourceId().isEmpty());
    QCOMPARE(state.sources().size(), 1);
    QCOMPARE(QImage(path), image);
    QVERIFY(state.removeSource("b"));
    QCOMPARE(state.sources().size(), 0);
    QVERIFY(!state.canMoveCurrentSourceUp());
    QVERIFY(!state.canMoveCurrentSourceDown());
    QVERIFY(QFile::exists(path));
    QCOMPARE(QImage(path), image);
}

QTEST_GUILESS_MAIN(VideoSourceManagerTest)
#include "tst_videosourcemanager.moc"

void VideoSourceManagerTest::insertsSourcesAtTop()
{
    VideoSourceManager model;
    QSignalSpy inserted(&model, &VideoSourceManager::sourcesChanged);
    QSignalSpy changed(&model, &VideoSourceManager::sourceChanged);
    QSignalSpy availability(&model, &VideoSourceManager::currentSourceMoveAvailabilityChanged);
    QVERIFY(!addSource(model, {" ", "Invalid"}));
    QVERIFY(addSource(model, {"one", "First", "one.png"}));
    QVERIFY(!addSource(model, {"one", "Duplicate"}));
    QVERIFY(addSource(model, {"two", "Second", "one.png"}));
    QCOMPARE(model.sources().size(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.currentSourceId(), QString("two"));
    QCOMPARE(model.sources().at(0)->id(), QString("two"));
    QVERIFY(model.canMoveCurrentSourceDown());
    QVERIFY(!model.canMoveCurrentSourceUp());
    QVERIFY(!availability.isEmpty());
    QVERIFY(setSourcePixelSize(model, "two", QSize(40, 20)));
    QVERIFY(setSourcePixelSize(model, "two", QSize(40, 20)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!setSourcePixelSize(model, "missing", QSize(40, 20)));
}

void VideoSourceManagerTest::initializesAndValidatesTransform()
{
    VideoSourceManager model;
    QVERIFY(addSource(model, {"image", "Image", "image.png"}));
    const auto current = [&] {
        return model.sources().at(0)->transform();
    };
    QVERIFY(current().size().isEmpty());
    QVERIFY(setSourcePixelSize(model, "image", QSize(4000, 2000)));
    QCOMPARE(current().center(), QPointF(960, 540));
    QCOMPARE(current().size(), QSizeF(1920, 960));
    VideoSourceTransform edited{{-20.5, 95.25}, {300.5, 100.25}, -30};
    QSignalSpy changes(&model, &VideoSourceManager::sourceChanged);
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(current().rotationDegrees(), 330.0);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.last().at(0).toString(), QString("image"));
    edited.setRotationDegrees(690);
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(changes.size(), 1);
    QVERIFY(setSourcePixelSize(model, "image", QSize(600, 200)));
    QCOMPARE(current().center(), edited.center());
    QCOMPARE(current().size(), edited.size());
    const VideoSourceTransform before = current();
    QVERIFY(!model.setSourceTransform("missing", before));
    QVERIFY(model.setSourceLocked("image", true));
    edited.setCenter(edited.center() + QPointF(20, 20));
    QVERIFY(!model.setSourceTransform("image", edited));
    QVERIFY(model.setSourceLocked("image", false));
    for (int part = 0; part < 5; ++part) {
        auto invalid = before;
        if (part == 0)
            invalid.setCenter(QPointF(std::numeric_limits<qreal>::infinity(), before.center().y()));
        if (part == 1)
            invalid.setCenter(QPointF(before.center().x(), std::numeric_limits<qreal>::quiet_NaN()));
        if (part == 2)
            invalid.setSize(QSizeF(.5, before.size().height()));
        if (part == 3)
            invalid.setSize(QSizeF(before.size().width(), -1));
        if (part == 4)
            invalid.setRotationDegrees(std::numeric_limits<qreal>::quiet_NaN());
        QVERIFY(!model.setSourceTransform("image", invalid));
        QVERIFY(current() == before);
    }
    VideoSourceManager ready;
    QVERIFY(addSources(ready, {{"ready", "Ready", "ready.png"}}));
    setSourcePixelSize(ready, "ready", {40, 20});
    QCOMPARE(ready.sources().at(0)->transform().size(), QSizeF(40, 20));
}

void VideoSourceManagerTest::mapsSourceCoordinates()
{
    const auto thin = VideoSourceTransform(QSizeF(1, 4000));
    QVERIFY(thin.isValid());
    QCOMPARE(thin.size().height(), 1080.0);
    QCOMPARE(thin.size().width(), .27);
    for (qreal angle : {0., 37., 90., 225.}) {
        const VideoSourceTransform start{{960, 540}, {400, 200}, angle};
        const auto matrix = start.sourceToCanvas();
        QVERIFY(start.contains(matrix.map(QPointF(200, 100))));
        QVERIFY(start.contains(matrix.map(QPointF(10, 10))));
        QVERIFY(!start.contains(matrix.map(QPointF(-10, 100))));
    }
}

void VideoSourceManagerTest::filterSelectionFollowsSourceAndFilterLifetime()
{
    VideoSourceManager manager;
    QVERIFY(addSource(manager, {"a", "A", "a.png"}));
    QVERIFY(addSource(manager, {"b", "B", "b.png"}));
    const auto first = manager.addFilter("a");
    const auto second = manager.addFilter("a");
    const auto other = manager.addFilter("b");
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !other.isEmpty());
    QSignalSpy selected(&manager, &VideoSourceManager::currentFilterIdChanged);
    QVERIFY(manager.currentFilterId("a").isEmpty());
    QVERIFY(manager.selectFilter("a", {}));
    QVERIFY(!manager.selectFilter("missing", {}));
    QVERIFY(!manager.selectFilter({}, first));
    QVERIFY(!manager.selectFilter("a", "missing"));
    QVERIFY(!manager.selectFilter("a", other));
    QCOMPARE(selected.count(), 0);
    QVERIFY(manager.selectFilter("a", second));
    QCOMPARE(selected.count(), 1);
    QCOMPARE(selected.last().at(0).toString(), QString("a"));
    QVERIFY(manager.selectFilter("a", second));
    QCOMPARE(selected.count(), 1);
    QVERIFY(manager.selectFilter("b", other));
    QCOMPARE(manager.currentFilterId("a"), second);
    QCOMPARE(manager.currentFilterId("b"), other);
    QVERIFY(manager.moveFilter("a", second, -1));
    QVERIFY(manager.moveSource("a", -1));
    QVERIFY(manager.removeFilter("a", first));
    QCOMPARE(manager.currentFilterId("a"), second);
    QCOMPARE(selected.count(), 2);
    QVERIFY(!manager.setSourceFilters("a", {VideoSourceFilter{}}));
    QCOMPARE(manager.currentFilterId("a"), second);
    QVERIFY(manager.setSourceFilters("a", {VideoSourceFilter{"replacement"}}));
    QVERIFY(manager.currentFilterId("a").isEmpty());
    QCOMPARE(selected.count(), 3);
    QVERIFY(manager.selectFilter("a", "replacement"));
    QVERIFY(manager.removeFilter("a", "replacement"));
    QVERIFY(manager.currentFilterId("a").isEmpty());
    QVERIFY(manager.selectFilter("b", {}));
    QVERIFY(manager.currentFilterId("b").isEmpty());
    QVERIFY(manager.selectFilter("b", other));
    QVERIFY(manager.removeSource("b"));
    QVERIFY(manager.currentFilterId("b").isEmpty());
    QVERIFY(addSource(manager, {"b", "B", "b.png"}));
    QVERIFY(manager.setSourceFilters("b", {VideoSourceFilter{other}}));
    QVERIFY(manager.currentFilterId("b").isEmpty());
}

void VideoSourceManagerTest::filterMoveAvailability()
{
    VideoSourceManager manager;
    QVERIFY(addSource(manager, {"image", "Image", "image.png"}));
    QVERIFY(addSource(manager, {"other", "Other", "other.png"}));
    QVERIFY(!manager.canMoveFilterUp("image", {}));
    QVERIFY(!manager.canMoveFilterDown("image", {}));
    const auto first = manager.addFilter("image");
    QVERIFY(!first.isEmpty());
    QVERIFY(!manager.canMoveFilterUp("image", first));
    QVERIFY(!manager.canMoveFilterDown("image", first));
    const auto middle = manager.addFilter("image");
    const auto last = manager.addFilter("image");
    QVERIFY(!middle.isEmpty() && !last.isEmpty());
    QVERIFY(!manager.canMoveFilterUp("image", first));
    QVERIFY(manager.canMoveFilterDown("image", first));
    QVERIFY(manager.canMoveFilterUp("image", middle));
    QVERIFY(manager.canMoveFilterDown("image", middle));
    QVERIFY(manager.canMoveFilterUp("image", last));
    QVERIFY(!manager.canMoveFilterDown("image", last));
    for (const auto &id : {QString(), QString("missing"), QString("other")}) {
        QVERIFY(!manager.canMoveFilterUp(id, first));
        QVERIFY(!manager.canMoveFilterDown(id, first));
    }
    QVERIFY(!manager.canMoveFilterUp("image", "missing"));
    QVERIFY(!manager.canMoveFilterDown("image", "missing"));
    QVERIFY(manager.moveFilter("image", first, 1));
    QVERIFY(manager.canMoveFilterUp("image", first));
    QVERIFY(!manager.canMoveFilterUp("image", middle));
    QVERIFY(manager.removeFilter("image", last));
    QVERIFY(!manager.canMoveFilterDown("image", first));
    QVERIFY(manager.removeSource("image"));
    QVERIFY(!manager.canMoveFilterUp("image", first));
    QVERIFY(!manager.canMoveFilterDown("image", first));
}

void VideoSourceManagerTest::filtersAreOrderedValidatedAndIndependent()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, QList<SourceFixture>{{"a", "A", "a.png"},
                                                   {"b", "B", "a.png"},
                                                   {"camera", "Camera", cameraRequest("device")}}));
    QSignalSpy changed(&model, &VideoSourceManager::sourceFiltersChanged);
    QSignalSpy data(&model, &VideoSourceManager::sourceChanged);
    QVERIFY(!model.canFilterCurrentSource());
    QVERIFY(model.selectSource("a"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(model.canFilterSource("camera"));
    QVERIFY(model.addFilter("missing").isEmpty());
    QVERIFY(model.addFilter("a", static_cast<VideoSourceFilterType>(-1)).isEmpty());
    QVERIFY(model.sourceFilters("a").isEmpty());
    QCOMPARE(changed.count(), 0);
    const QString first = model.addFilter("a", VideoSourceFilterType::ColorCorrection);
    const QString second = model.addFilter("a");
    QVERIFY(!first.isEmpty() && first != second);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(data.last().at(0).toString(), QString("a"));
    QCOMPARE(model.sourceFilters("a")[0].type(), VideoSourceFilterType::ColorCorrection);
    QVERIFY(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[0].config()).isValid());
    QVERIFY(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[0].config()).isIdentity());
    QCOMPARE(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[0].config()).brightness(), 0.f);
    QCOMPARE(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[0].config()).contrast(), 1.f);
    QCOMPARE(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[0].config()).saturation(), 1.f);
    QVERIFY(model.sourceFilters("b").isEmpty());
    QVERIFY(model.setSourceLocked("a", true));
    QVERIFY(setFilterConfig(model, "a", first, ColorCorrectionConfig{.2f, 1.5f, 0}));
    QVERIFY(setFilterEnabled(model, "a", first, false));
    const int before = changed.count();
    QVERIFY(setFilterEnabled(model, "a", first, false));
    QVERIFY(setFilterConfig(model, "a", first, ColorCorrectionConfig{.2f, 1.5f, 0}));
    QCOMPARE(changed.count(), before);
    for (double invalid : {-1.01, 1.01, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()})
        QVERIFY(!setFilterConfig(model, "a", first, ColorCorrectionConfig{float(invalid), 1, 1}));
    for (double invalid : {-.01, 2.01, std::numeric_limits<double>::infinity(),
                           std::numeric_limits<double>::quiet_NaN()}) {
        QVERIFY(!setFilterConfig(model, "a", first, ColorCorrectionConfig{0, float(invalid), 1}));
        QVERIFY(!setFilterConfig(model, "a", first, ColorCorrectionConfig{0, 1, float(invalid)}));
    }
    QCOMPARE(changed.count(), before);
    const auto unchanged = model.sourceFilters("a")[0];
    QVERIFY(std::get<ColorCorrectionConfig>(unchanged.config()).isValid());
    QVERIFY(!std::get<ColorCorrectionConfig>(unchanged.config()).isIdentity());
    QVERIFY((std::get<ColorCorrectionConfig>(unchanged.config()) ==
             ColorCorrectionConfig{.2f, 1.5f, 0}));
    QVERIFY(!setFilterConfig(model, "a", "missing", ColorCorrectionConfig{}));
    QVERIFY(!model.moveFilter("a", first, -1));
    QVERIFY(!model.moveFilter("a", second, 1));
    QVERIFY(!model.moveFilter("a", first, 2));
    QVERIFY(model.moveFilter("a", first, 1));
    QCOMPARE(model.sourceFilters("a")[1].id(), first);
    QVERIFY(setFilterEnabled(model, "a", first, true));
    QCOMPARE(std::get<ColorCorrectionConfig>(model.sourceFilters("a")[1].config()).brightness(), .2f);
    const auto typed = model.sources().at(0)->filters();
    QCOMPARE(typed[1].id(), first);
    QCOMPARE(std::get<ColorCorrectionConfig>(typed[1].config()).saturation(), 0.f);
    QVERIFY(!setFilterEnabled(model, "b", first, false));
    QVERIFY(!setFilterConfig(model, "missing", first, ColorCorrectionConfig{0, 1, 1}));
    QVERIFY(!model.moveFilter("missing", first, 1));
    QVERIFY(!model.removeFilter("a", "missing"));
    QVERIFY(model.removeFilter("a", first));
    QCOMPARE(model.sourceFilters("a").size(), 1);
    QVERIFY(model.removeSource("a"));
    QVERIFY(model.sourceFilters("a").isEmpty());
    QVERIFY(!model.removeFilter("a", second));
}

void VideoSourceManagerTest::captureSettingUpdatesPreserveSourceState()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, QList<SourceFixture>{{"screen", "Screen", true},
                                                   {"image", "Image", "a.png"},
                                                   {"camera", "Camera", cameraRequest("device")}}));
    QSignalSpy changed(&model, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&model, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(model.selectSource("screen"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(setSourcePixelSize(model, "screen", QSize(1920, 1080)));
    QCOMPARE(status.count(), 1);
    status.clear();
    const VideoSourceTransform transform{{300, 500}, {640, 360}, 45};
    QVERIFY(model.setSourceTransform("screen", transform));
    const auto filter = model.addFilter("screen");
    QVERIFY(model.setSourceLocked("screen", true));
    const int before = changed.count();
    QVERIFY(!setDisplayCursor(model, "image", true));
    QVERIFY(!setDisplayCursor(model, "missing", true));
    QVERIFY(setDisplayCursor(model, "screen", true));
    QCOMPARE(changed.count(), before);
    QVERIFY(setDisplayCursor(model, "screen", false));
    QCOMPARE(changed.last().at(0).toString(), QString("screen"));
    QCOMPARE(model.sources().at(0)->transform(), transform);
    QCOMPARE(model.sourceFilters("screen")[0].id(), filter);
    QVERIFY(!displaySource(model, "screen")->captureCursor());
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Error, "Disconnected"));
    QCOMPARE(status.count(), 1);
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Error, "Disconnected"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(model.sourceState(model.sources().at(0)->id()), VideoSourceState::Error);
    QCOMPARE(model.sourceError("screen"), QString("Disconnected"));
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Starting, {}));
    QVERIFY(model.sourceError("screen").isEmpty());
    QVERIFY(model.selectSource("image"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(model.selectSource("camera"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(model.removeSource("screen"));
    QVERIFY(!displaySource(model, "screen"));
    QVERIFY(!setSourceStatus(model, "screen", VideoSourceState::Error, "Removed"));
}

void VideoSourceManagerTest::sourceRemovalDuringGeometryNotificationIsSafe()
{
    VideoSourceManager manager;
    QVERIFY(addSource(manager, {"camera", "Camera", cameraRequest("device")}));
    connect(&manager, &VideoSourceManager::sourceChanged, this, [&](const QString &sourceId) {
        manager.removeSource(sourceId);
    });
    setCameraActualMode(manager, "camera", QSize(1280, 720), 30, 1);
    QVERIFY(manager.sources().isEmpty());
}

void VideoSourceManagerTest::sourceReorderingDuringGeometryNotificationIsSafe()
{
    VideoSourceManager manager;
    QVERIFY(addSources(
        manager, {{"camera", "Camera", cameraRequest("device")}, {"image", "Image", "image.png"}}));
    connect(&manager, &VideoSourceManager::sourceChanged, this, [&](const QString &sourceId) {
        QVERIFY(manager.moveSource(sourceId, 1));
    });
    QVERIFY(setCameraActualMode(manager, "camera", QSize(1280, 720), 30000, 1001));
    QCOMPARE(manager.sources().at(1)->id(), QString("camera"));
    QCOMPARE(findSource(manager, "camera")->pixelSize(), QSize(1280, 720));
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateNumerator(), 30000);
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateDenominator(), 1001);
    QVERIFY(!cameraSource(manager, "image"));
}

void VideoSourceManagerTest::topLevelProperties()
{
    VideoSourceManager manager;
    QVERIFY(addSource(manager, {"a", "A", "a.png"}));
    QVERIFY(addSource(manager, {"b", "B", "b.png"}));
    QSignalSpy changed(&manager, &VideoSourceManager::sourceChanged);
    QVERIFY(manager.setSourceName("a", " Renamed "));
    QCOMPARE(manager.sourceName("a"), QString("Renamed"));
    QVERIFY(manager.setSourceName("a", "Renamed"));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!manager.setSourceName("a", " b "));
    QVERIFY(!manager.setSourceName("a", " "));
    QVERIFY(!manager.setSourceName("missing", "Name"));
    QVERIFY(manager.sourceVisible("a"));
    QVERIFY(!manager.sourceLocked("a"));
    QVERIFY(manager.setSourceVisible("a", false));
    QVERIFY(!manager.sourceVisible("a"));
    QVERIFY(manager.setSourceLocked("a", true));
    QVERIFY(manager.sourceLocked("a"));
    QVERIFY(!manager.sourceVisible("missing"));
    QVERIFY(!manager.sourceLocked("missing"));
    QVERIFY(manager.sourceTransform("missing").size().isEmpty());
    QVERIFY(!manager.source("missing"));
    QVERIFY(!manager.setImageFilePath("missing", "other.png"));
    QVERIFY(!manager.setImageFilePath("a", QString()));
    QVERIFY(!manager.setDisplayCaptureCursor("a", true));
    QVERIFY(manager.setImageFilePath("a", "other.png"));
    QCOMPARE(imageSource(manager, "a")->filePath(), QString("other.png"));

    QSignalSpy statusChanged(&manager, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(manager.updateSourceStatus("a", {640, 360}, VideoSourceState::Ready));
    QCOMPARE(statusChanged.count(), 1);
    QVERIFY(manager.updateSourceStatus("a", {640, 360}, VideoSourceState::Ready));
    QCOMPARE(statusChanged.count(), 1);
    QCOMPARE(manager.sourceTransform("a").size(), QSizeF(640, 360));
    QCOMPARE(manager.sourceState("a"), VideoSourceState::Ready);
    QVERIFY(!manager.updateSourceStatus("missing", {640, 360}, VideoSourceState::Ready));

    QSignalSpy filtersChanged(&manager, &VideoSourceManager::sourceFiltersChanged);
    const QList<VideoSourceFilter> filters{VideoSourceFilter{"filter"}};
    QVERIFY(manager.setSourceFilters("a", filters));
    QCOMPARE(manager.sourceFilters("a")[0].id(), QString("filter"));
    QVERIFY(manager.setSourceFilters("a", filters));
    QCOMPARE(filtersChanged.count(), 1);
    QVERIFY(!manager.setSourceFilters("missing", filters));
    QVERIFY(!manager.setSourceFilters("a", {VideoSourceFilter{}}));
    QVERIFY(!manager.setSourceFilters("a", {filters[0], filters[0]}));
    QCOMPARE(manager.sourceFilters("a").size(), 1);
}

void VideoSourceManagerTest::ownsSourcesAndKeepsPointersStable()
{
    bool destroyed = false;

    struct TrackedSource : VideoSource
    {
        TrackedSource(bool &destroyed) : VideoSource("tracked", "Tracked"), destroyed(destroyed)
        {
        }

        ~TrackedSource() override
        {
            destroyed = true;
        }

        VideoSourceType type() const override
        {
            return VideoSourceType::Image;
        }

        bool &destroyed;
    };

    VideoSourceManager manager;
    QVERIFY(!manager.addSource(nullptr));
    auto owned = std::make_unique<TrackedSource>(destroyed);
    const VideoSource *identity = owned.get();
    QVERIFY(manager.addSource(std::move(owned)));
    QVERIFY(!owned);
    QVERIFY(!destroyed);
    QVERIFY(manager.addSource(std::make_unique<ImageSource>("image", "Image", "image.png")));
    QCOMPARE(manager.source("tracked"), identity);
    QVERIFY(manager.moveSource("tracked", -1));
    QCOMPARE(manager.source("tracked"), identity);
    const auto ids = manager.sourceIds();
    connect(&manager, &VideoSourceManager::sourceStatusChanged, this, [&](const QString &id) {
        manager.removeSource(id);
    });
    for (const auto &id : ids)
        QVERIFY(manager.setSourceState(id, VideoSourceState::Error, "Removed by subscriber"));
    QVERIFY(destroyed);
    QVERIFY(manager.sources().isEmpty());
}

void VideoSourceManagerTest::cameraCaptureNotificationsIncludeMetadata()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource(std::make_unique<CameraSource>("camera", "Camera", "device")));
    QVERIFY(manager.addSource(std::make_unique<ImageSource>("image", "Image", "image.png")));
    QSignalSpy status(&manager, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(manager.updateCameraCapture("camera", {640, 480}, 30, 1, "MJPEG"));
    QCOMPARE(status.count(), 1);
    QVERIFY(manager.updateCameraCapture("camera", {640, 480}, 30, 1, "MJPEG"));
    QCOMPARE(status.count(), 1);
    QVERIFY(
        manager.updateCameraCapture("camera", {640, 480}, 30, 1, "MJPEG: dropped damaged frame"));
    QCOMPARE(status.count(), 2);
    QVERIFY(!manager.updateCameraCapture("image", {640, 480}, 30, 1, "MJPEG"));
    QVERIFY(!manager.updateCameraCapture("missing", {640, 480}, 30, 1, "MJPEG"));
    QVERIFY(manager.setSourceState("camera", VideoSourceState::Error, "Disconnected"));
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateNumerator(), 30);
    QCOMPARE(manager.source("camera")->pixelSize(), QSize(640, 480));
    QVERIFY(manager.updateCameraCapture("camera", {1280, 720}, 30000, 1001, "Reconnected"));
    QVERIFY(manager.source("camera")->error().isEmpty());
    QCOMPARE(cameraSource(manager, "camera")->negotiatedFrameRateDenominator(), 1001);
}

void VideoSourceManagerTest::selectionCallbacksObserveRemovedSource()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource(std::make_unique<DisplaySource>("screen", "Screen")));
    connect(&manager, &VideoSourceManager::currentSourceIdChanged, this, [&] {
        QVERIFY(!manager.source("screen"));
        QVERIFY(!manager.removeSource("screen"));
    });
    QVERIFY(manager.removeSource(manager.source("screen")->id()));
    QVERIFY(manager.sourceIds().isEmpty());
}
