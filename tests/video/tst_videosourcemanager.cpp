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
    void displayPropertiesPreserveSourceState();
    void initializesAndValidatesCanvasGeometry();
    void mapsSourceCoordinates();
    void startsWithEmptySourceList();
    void exposesTypedSourcesAndRejectsMissingIds();
    void supportsAllSourceConfigs();
    void runtimeDetailsFollowSourceType();
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
    for (const auto &source : manager.sources()) {
        if (source.id() == sourceId)
            return &source;
    }
    return nullptr;
}

static QList<VideoSource> sources()
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
    QVERIFY(addSources(manager, {{"image", "Logo", false, true, {}, {}, {},
                                ImageSourceConfig{"fixture.png"}}}));
    QCOMPARE(manager.sources().size(), 1);
    QCOMPARE(manager.sourceName("image"), QString("Logo"));
    QVERIFY(manager.sourceName("missing").isNull());
    QVERIFY(manager.sourceName({}).isNull());
    const auto *source = findSource(manager, "image");
    QVERIFY(source);
    QCOMPARE(source->id(), QString("image"));
    QCOMPARE(source->name(), QString("Logo"));
    QVERIFY(setSourcePixelSize(manager, "image", {64, 32}));
    QCOMPARE(manager.sourceRuntime("image").pixelSize(), QSize(64, 32));
    QCOMPARE(findSource(manager, "image")->runtime().pixelSize(), QSize(64, 32));
    QVERIFY(!source->visible());
    QVERIFY(source->locked());
    QCOMPARE(std::get<ImageSourceConfig>(source->config()).filePath(), QString("fixture.png"));
    QVERIFY(!findSource(manager, "missing"));
    QVERIFY(!cameraSource(manager, "image"));
    QVERIFY(!displaySource(manager, "image"));
    QVERIFY(!cameraSource(manager, "missing"));
    QVERIFY(!displaySource(manager, "missing"));
    QVERIFY(!findSource(manager, {}));
    QVERIFY(manager.addSource({"unnamed", {}}));
    QVERIFY(manager.sourceName("unnamed").isEmpty());
    QVERIFY(!manager.sourceName("unnamed").isNull());
}

void VideoSourceManagerTest::supportsAllSourceConfigs()
{
    QList<VideoSource> entries{
        {"display", "Display", DisplaySourceConfig{"monitor-1", false}},
        {"camera", "Camera", CameraSourceConfig{"camera-1", QSize(1280, 720), 30, 1}},
        {"image", "Image",
         ImageSourceConfig{"fixture.png"}}};
    VideoSourceManager manager;
    QVERIFY(addSources(manager, entries));
    setSourcePixelSize(manager, "image", {64, 32});
    QCOMPARE(findSource(manager, "display")->type(), VideoSourceType::DisplayCapture);
    QCOMPARE(findSource(manager, "camera")->type(), VideoSourceType::Camera);
    QCOMPARE(findSource(manager, "image")->type(), VideoSourceType::Image);
    QCOMPARE(manager.sourceRuntime("image").pixelSize(), QSize(64, 32));
    for (const auto &entry : entries) {
        QVERIFY(manager.selectSource(entry.id()));
        QVERIFY(manager.setSourceVisible(entry.id(), false));
        QVERIFY(manager.setSourceLocked(entry.id(), true));
    }
    entries[0].setConfig(ImageSourceConfig{"replacement.png"});
    QCOMPARE(findSource(manager, "display")->type(), VideoSourceType::DisplayCapture);
    QVERIFY(manager.moveSource("image", -1));
    QCOMPARE(manager.sources().at(1).id(), QString("image"));
    for (const auto &source : manager.sources()) {
        QVERIFY(!source.visible());
        QVERIFY(source.locked());
    }
    QVERIFY(manager.removeSource("image"));
    QCOMPARE(manager.sources().at(1).type(), VideoSourceType::Camera);
}

void VideoSourceManagerTest::runtimeDetailsFollowSourceType()
{
    VideoSource camera{"camera", "Camera", CameraSourceConfig{"device"}};
    VideoSource image{"image", "Image", ImageSourceConfig{"image.png"}};
    VideoSourceManager manager;
    QVERIFY(addSources(manager, {camera, image}));
    QVERIFY(setCameraActualMode(manager, "camera", {1280, 720}, 30000, 1001));
    QCOMPARE(manager.sourceRuntime("camera").actualFrameRateNumerator(), 30000);
    QCOMPARE(manager.sourceRuntime("camera").actualFrameRateDenominator(), 1001);
    QVERIFY(!setCameraActualMode(manager, "image", {800, 600}, 30, 1));
    QVERIFY(!setSourceStatus(manager, "missing", VideoSourceState::Ready, {}));
    QVERIFY(!setSourcePixelSize(manager, "missing", {64, 32}));
    QSignalSpy changed(&manager, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&manager, &VideoSourceManager::sourceRuntimeChanged);
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Error, "Load failed"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.last().at(0).toString(), QString("image"));
    QCOMPARE(manager.sourceRuntime("image").error(), QString("Load failed"));
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Error, "Load failed"));
    QCOMPARE(status.count(), 1);
    QVERIFY(setSourceStatus(manager, "image", VideoSourceState::Ready, {}));
    QVERIFY(manager.sourceRuntime("image").error().isEmpty());
    QCOMPARE(changed.count(), 0);
    // Runtime travels with source snapshots and is copied independently.
    VideoSourceManager copy;
    QVERIFY(addSources(copy, manager.sources()));
    QCOMPARE(copy.sourceRuntime("camera").pixelSize(), QSize(1280, 720));
    QCOMPARE(findSource(copy, "camera")->runtime().actualFrameRateNumerator(), 30000);
    QCOMPARE(findSource(copy, "camera")->runtime().actualFrameRateDenominator(), 1001);
    QCOMPARE(findSource(copy, "image")->runtime().state(), VideoSourceState::Ready);
    QVERIFY(setSourceStatus(copy, "image", VideoSourceState::Error, "Copy failed"));
    QCOMPARE(findSource(manager, "image")->runtime().state(), VideoSourceState::Ready);
    QVERIFY(findSource(manager, "image")->runtime().error().isEmpty());
    QVERIFY(manager.moveSource("camera", 1));
    QCOMPARE(manager.sourceRuntime("camera").pixelSize(), QSize(1280, 720));
    QVERIFY(manager.removeSource("camera"));
    QVERIFY(manager.sourceRuntime("camera").pixelSize().isEmpty());
    QVERIFY(manager.addSource(camera));
    QVERIFY(manager.sourceRuntime("camera").state() == VideoSourceState::Idle);
    QCOMPARE(manager.sourceRuntime("camera").actualFrameRateNumerator(), 0);
    QVERIFY(manager.sourceRuntime("image").state() == VideoSourceState::Ready);
    camera.setRuntime({{640, 360}, VideoSourceState::Capturing, {}, 30, 1});
    QVERIFY(manager.removeSource("camera"));
    QVERIFY(manager.addSource(camera));
    QCOMPARE(findSource(manager, "camera")->runtime().pixelSize(), QSize(640, 360));
    QCOMPARE(findSource(manager, "camera")->runtime().actualFrameRateNumerator(), 30);
    QCOMPARE(findSource(manager, "camera")->canvasGeometry().size(), QSizeF(640, 360));
    VideoSourceManager initialized;
    QVERIFY(addSources(initialized, {camera}));
    QCOMPARE(findSource(initialized, "camera")->runtime().state(), VideoSourceState::Capturing);
    QCOMPARE(findSource(initialized, "camera")->canvasGeometry().size(), QSizeF(640, 360));
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
    const QString id = manager.sources().at(row).id();
    const QString survivorId = manager.sources().at((row + 1) % 3).id();
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
        QCOMPARE(manager.sources().at(1).id(), QString("c"));
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
    QCOMPARE(manager.sources().at(1).id(), QString("a"));
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
    QVERIFY(addSources(state, QList<VideoSource>{{"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}}));
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
    QList<VideoSource> entries{
        {"a", "First", ImageSourceConfig{path}},
        {"b", "Second", ImageSourceConfig{path}}};
    VideoSourceManager state;
    QVERIFY(addSources(state, entries));
    setSourcePixelSize(state, "a", image.size());
    setSourcePixelSize(state, "b", image.size());
    entries.clear(); // State owns its source list.
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto other = *findSource(state, "b");
    QCOMPARE(other.id(), QString("b"));
    QCOMPARE(state.sourceRuntime(other.id()).pixelSize(), image.size());
    QVERIFY(other.visible());
    QVERIFY(!other.locked());
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
    QVERIFY(!model.addSource({" ", "Invalid"}));
    QVERIFY(model.addSource({"one", "First", ImageSourceConfig{"one.png"}}));
    QVERIFY(!model.addSource({"one", "Duplicate"}));
    QVERIFY(model.addSource({"two", "Second", ImageSourceConfig{"one.png"}}));
    QCOMPARE(model.sources().size(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.currentSourceId(), QString("two"));
    QCOMPARE(model.sources().at(0).id(), QString("two"));
    QVERIFY(model.canMoveCurrentSourceDown());
    QVERIFY(!model.canMoveCurrentSourceUp());
    QVERIFY(!availability.isEmpty());
    QVERIFY(setSourcePixelSize(model, "two", QSize(40, 20)));
    QVERIFY(setSourcePixelSize(model, "two", QSize(40, 20)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!setSourcePixelSize(model, "missing", QSize(40, 20)));
}

void VideoSourceManagerTest::initializesAndValidatesCanvasGeometry()
{
    VideoSourceManager model;
    QVERIFY(model.addSource({"image", "Image", ImageSourceConfig{"image.png"}}));
    const auto current = [&] {
        return model.sources().at(0).canvasGeometry();
    };
    QVERIFY(current().size().isEmpty());
    QVERIFY(setSourcePixelSize(model, "image", QSize(4000, 2000)));
    QCOMPARE(current().center(), QPointF(960, 540));
    QCOMPARE(current().size(), QSizeF(1920, 960));
    VideoSourceCanvasGeometry edited{{-20.5, 95.25}, {300.5, 100.25}, -30};
    QSignalSpy changes(&model, &VideoSourceManager::sourceChanged);
    QVERIFY(model.setSourceCanvasGeometry("image", edited));
    QCOMPARE(current().rotation(), 330.0);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.last().at(0).toString(), QString("image"));
    edited.setRotation(690);
    QVERIFY(model.setSourceCanvasGeometry("image", edited));
    QCOMPARE(changes.size(), 1);
    QVERIFY(setSourcePixelSize(model, "image", QSize(600, 200)));
    QCOMPARE(current().center(), edited.center());
    QCOMPARE(current().size(), edited.size());
    const VideoSourceCanvasGeometry before = current();
    QVERIFY(!model.setSourceCanvasGeometry("missing", before));
    QVERIFY(model.setSourceLocked("image", true));
    edited.setCenter(edited.center() + QPointF(20, 20));
    QVERIFY(!model.setSourceCanvasGeometry("image", edited));
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
            invalid.setRotation(std::numeric_limits<qreal>::quiet_NaN());
        QVERIFY(!model.setSourceCanvasGeometry("image", invalid));
        QVERIFY(current() == before);
    }
    VideoSourceManager ready;
    QVERIFY(addSources(ready, {{"ready", "Ready",
          ImageSourceConfig{"ready.png"}}}));
    setSourcePixelSize(ready, "ready", {40, 20});
    QCOMPARE(ready.sources().at(0).canvasGeometry().size(), QSizeF(40, 20));
}

void VideoSourceManagerTest::mapsSourceCoordinates()
{
    const auto thin = VideoSourceCanvasGeometry(QSizeF(1, 4000));
    QVERIFY(thin.isValid());
    QCOMPARE(thin.size().height(), 1080.0);
    QCOMPARE(thin.size().width(), .27);
    for (qreal angle : {0., 37., 90., 225.}) {
        const VideoSourceCanvasGeometry start{{960, 540}, {400, 200}, angle};
        const auto matrix = start.sourceToCanvas();
        QVERIFY(start.contains(matrix.map(QPointF(200, 100))));
        QVERIFY(start.contains(matrix.map(QPointF(10, 10))));
        QVERIFY(!start.contains(matrix.map(QPointF(-10, 100))));
    }
}

void VideoSourceManagerTest::filterSelectionFollowsSourceAndFilterLifetime()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource({"a", "A", ImageSourceConfig{"a.png"}}));
    QVERIFY(manager.addSource({"b", "B", ImageSourceConfig{"b.png"}}));
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
    QVERIFY(manager.addSource({"b", "B", ImageSourceConfig{"b.png"}}));
    QVERIFY(manager.setSourceFilters("b", {VideoSourceFilter{other}}));
    QVERIFY(manager.currentFilterId("b").isEmpty());
}

void VideoSourceManagerTest::filterMoveAvailability()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource({"image", "Image", ImageSourceConfig{"image.png"}}));
    QVERIFY(manager.addSource({"other", "Other", ImageSourceConfig{"other.png"}}));
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
    QVERIFY(addSources(model, QList<VideoSource>{{"a", "A", ImageSourceConfig{"a.png"}},
                           {"b", "B", ImageSourceConfig{"a.png"}},
                           {"camera", "Camera", CameraSourceConfig{"device"}}}));
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
    const auto typed = model.sources().at(0).filters();
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

void VideoSourceManagerTest::displayPropertiesPreserveSourceState()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, QList<VideoSource>{
        {"screen", "Screen", DisplaySourceConfig{"physical-path", true}},
        {"image", "Image", ImageSourceConfig{"a.png"}},
        {"camera", "Camera", CameraSourceConfig{"device"}}}));
    QSignalSpy changed(&model, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&model, &VideoSourceManager::sourceRuntimeChanged);
    QVERIFY(model.selectSource("screen"));
    QVERIFY(model.canEditCurrentSourceProperties() && model.canFilterCurrentSource());
    QVERIFY(setSourcePixelSize(model, "screen", QSize(1920, 1080)));
    QCOMPARE(status.count(), 1);
    status.clear();
    const VideoSourceCanvasGeometry canvasGeometry{{300, 500}, {640, 360}, 45};
    QVERIFY(model.setSourceCanvasGeometry("screen", canvasGeometry));
    const auto filter = model.addFilter("screen");
    QVERIFY(model.setSourceLocked("screen", true));
    const int before = changed.count();
    QVERIFY(!setDisplayConfig(model, "screen", " ", true));
    QVERIFY(!setDisplayConfig(model, "image", "another", true));
    QVERIFY(!setDisplayConfig(model, "missing", "another", true));
    QVERIFY(setDisplayConfig(model, "screen", "physical-path", true));
    QCOMPARE(changed.count(), before);
    QVERIFY(setDisplayConfig(model, "screen", "another-physical-path", false));
    QCOMPARE(changed.last().at(0).toString(), QString("screen"));
    QCOMPARE(model.sources().at(0).canvasGeometry(), canvasGeometry);
    QCOMPARE(model.sourceFilters("screen")[0].id(), filter);
    QCOMPARE(displaySource(model, "screen")->displayId(), QString("another-physical-path"));
    QVERIFY(!displaySource(model, "screen")->captureCursor());
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Error, "Disconnected"));
    QCOMPARE(status.count(), 1);
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Error, "Disconnected"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(model.sourceRuntime(model.sources().at(0).id()).state(),
             VideoSourceState::Error);
    QCOMPARE(model.sourceRuntime("screen").error(), QString("Disconnected"));
    QVERIFY(setSourceStatus(model, "screen", VideoSourceState::Starting, {}));
    QVERIFY(model.sourceRuntime("screen").error().isEmpty());
    QVERIFY(model.selectSource("image"));
    QVERIFY(!model.canEditCurrentSourceProperties() && model.canFilterCurrentSource());
    QVERIFY(model.selectSource("camera"));
    QVERIFY(model.canEditCurrentSourceProperties() && model.canFilterCurrentSource());
    QVERIFY(model.removeSource("screen"));
    QVERIFY(!displaySource(model, "screen"));
    QVERIFY(!setSourceStatus(model, "screen", VideoSourceState::Error, "Removed"));
}

void VideoSourceManagerTest::sourceRemovalDuringGeometryNotificationIsSafe()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource({"camera", "Camera", CameraSourceConfig{"device"}}));
    connect(&manager, &VideoSourceManager::sourceChanged, this, [&](const QString &sourceId) {
        manager.removeSource(sourceId);
    });
    setCameraActualMode(manager, "camera", QSize(1280, 720), 30, 1);
    QVERIFY(manager.sources().isEmpty());
}

void VideoSourceManagerTest::sourceReorderingDuringGeometryNotificationIsSafe()
{
    VideoSourceManager manager;
    QVERIFY(addSources(manager, {
        {"camera", "Camera", CameraSourceConfig{"device"}},
        {"image", "Image", ImageSourceConfig{"image.png"}}}));
    connect(&manager, &VideoSourceManager::sourceChanged, this, [&](const QString &sourceId) {
        QVERIFY(manager.moveSource(sourceId, 1));
    });
    QVERIFY(setCameraActualMode(manager, "camera", QSize(1280, 720), 30000, 1001));
    QCOMPARE(manager.sources().at(1).id(), QString("camera"));
    QCOMPARE(findSource(manager, "camera")->runtime().pixelSize(), QSize(1280, 720));
    QCOMPARE(findSource(manager, "camera")->runtime().actualFrameRateNumerator(), 30000);
    QCOMPARE(findSource(manager, "camera")->runtime().actualFrameRateDenominator(), 1001);
    QCOMPARE(findSource(manager, "image")->runtime().actualFrameRateNumerator(), 0);
}

void VideoSourceManagerTest::topLevelProperties()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource({"a", "A", ImageSourceConfig{"a.png"}}));
    QVERIFY(manager.addSource({"b", "B", ImageSourceConfig{"b.png"}}));
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
    QVERIFY(manager.sourceCanvasGeometry("missing").size().isEmpty());
    QVERIFY(!manager.sourceConfig("missing"));
    QVERIFY(!manager.setSourceConfig("missing", ImageSourceConfig{"other.png"}));
    QVERIFY(!manager.setSourceConfig("a", ImageSourceConfig{}));
    QVERIFY(!manager.setSourceConfig("a", DisplaySourceConfig{"screen"}));
    QVERIFY(manager.setSourceConfig("a", ImageSourceConfig{"other.png"}));
    QCOMPARE(std::get<ImageSourceConfig>(*manager.sourceConfig("a")).filePath(), QString("other.png"));

    QSignalSpy runtimeChanged(&manager, &VideoSourceManager::sourceRuntimeChanged);
    VideoSourceRuntime runtime{{640, 360}, VideoSourceState::Ready, {}};
    QVERIFY(manager.setSourceRuntime("a", runtime));
    QCOMPARE(runtimeChanged.count(), 1);
    QVERIFY(manager.setSourceRuntime("a", runtime));
    QCOMPARE(runtimeChanged.count(), 1);
    QCOMPARE(manager.sourceCanvasGeometry("a").size(), QSizeF(640, 360));
    QCOMPARE(manager.sourceRuntime("a").state(), VideoSourceState::Ready);
    QVERIFY(!manager.setSourceRuntime("missing", runtime));

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
