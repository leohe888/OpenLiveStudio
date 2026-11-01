#include "video/videosourcemanager.h"

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
    void insertsSourcesAtTop();
    void sourceRemovalDuringGeometryNotificationIsSafe();
    void displayPropertiesPreserveSourceState();
    void initializesAndValidatesTransforms();
    void sharesGeometryForMappingAndResizing();
    void startsWithEmptySourceList();
    void exposesTypedSourcesAndRejectsMissingIds();
    void supportsAllSourceConfigurations();
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
    VideoSourceManager manager(
        {{"image", "Logo",
          ImageSourceDetails{ImageConfig{"fixture.png"}, ImageRuntime{QSize(64, 32)}}, false,
          true}});
    QCOMPARE(manager.sources().size(), 1);
    const auto *source = manager.source("image");
    QVERIFY(source);
    QCOMPARE(source->id, QString("image"));
    QCOMPARE(source->name, QString("Logo"));
    QCOMPARE(source->pixelSize(), QSize(64, 32));
    QVERIFY(!source->visible);
    QVERIFY(source->locked);
    QCOMPARE(std::get<ImageSourceDetails>(source->details).config.filePath, QString("fixture.png"));
    QVERIFY(!manager.source("missing"));
    QVERIFY(!manager.cameraSource("image"));
    QVERIFY(!manager.displaySource("image"));
    QVERIFY(!manager.cameraSource("missing"));
    QVERIFY(!manager.displaySource("missing"));
    QVERIFY(!manager.source({}));
}

void VideoSourceManagerTest::supportsAllSourceConfigurations()
{
    QList<VideoSource> entries{
        {"display", "Display", DisplaySourceDetails{DisplayConfig{"monitor-1", false}}},
        {"camera", "Camera", CameraSourceDetails{CameraConfig{"camera-1", QSize(1280, 720), 30}}},
        {"image", "Image",
         ImageSourceDetails{ImageConfig{"fixture.png"}, ImageRuntime{QSize(64, 32)}}}};
    VideoSourceManager manager(entries);
    QCOMPARE(manager.source("display")->type(), VideoSourceType::DisplayCapture);
    QCOMPARE(manager.source("camera")->type(), VideoSourceType::Camera);
    QCOMPARE(manager.source("image")->type(), VideoSourceType::Image);
    QCOMPARE(manager.source("image")->pixelSize(), QSize(64, 32));
    for (const auto &entry : entries) {
        QVERIFY(manager.selectSource(entry.id));
        QVERIFY(manager.setSourceVisible(entry.id, false));
        QVERIFY(manager.setSourceLocked(entry.id, true));
    }
    entries[0].details = ImageSourceDetails{ImageConfig{"replacement.png"}};
    QCOMPARE(manager.source("display")->type(), VideoSourceType::DisplayCapture);
    QVERIFY(manager.moveSource("image", -1));
    QCOMPARE(manager.sources().at(1).id, QString("image"));
    for (const auto &source : manager.sources()) {
        QVERIFY(!source.visible);
        QVERIFY(source.locked);
    }
    QVERIFY(manager.removeSource("image"));
    QCOMPARE(manager.sources().at(1).type(), VideoSourceType::Camera);
}

void VideoSourceManagerTest::runtimeDetailsFollowSourceType()
{
    VideoSource camera{
        "camera", "Camera",
        CameraSourceDetails{CameraConfig{"device"},
                            CameraRuntime{QSize(1280, 720), "capturing", {}, 30000, 1001}}};
    VideoSource image{"image", "Image",
                      ImageSourceDetails{ImageConfig{"image.png"},
                                         ImageRuntime{QSize(64, 32), "ready", {}}}};
    VideoSource display{
        "display", "Display",
        DisplaySourceDetails{DisplayConfig{"monitor"},
                             DisplayRuntime{QSize(1920, 1080), "error", "Display lost"}}};
    VideoSourceManager manager({camera, image, display});
    QCOMPARE(std::get<CameraSourceDetails>(manager.source("camera")->details)
                 .runtime.actualFrameRateNumerator,
             30000);
    QCOMPARE(std::get<CameraSourceDetails>(manager.source("camera")->details)
                 .runtime.actualFrameRateDenominator,
             1001);
    QCOMPARE(manager.source("display")->pixelSize(), QSize(1920, 1080));
    QCOMPARE(manager.source("image")->pixelSize(), QSize(64, 32));
    QSignalSpy changed(&manager, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&manager, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(!manager.setSourceStatus("image", "capturing", {}));
    QVERIFY(!manager.setSourceStatus("camera", "ready", {}));
    QVERIFY(!manager.setSourceStatus("display", "ready", {}));
    QVERIFY(!manager.setSourceStatus("missing", "ready", {}));
    manager.setCameraActualMode("image", {800, 600}, 30, 1);
    QCOMPARE(changed.count(), 0);
    QCOMPARE(status.count(), 0);
    QVERIFY(manager.setSourceStatus("image", "error", "Load failed"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(status.last().at(0).toString(), QString("image"));
    QCOMPARE(std::get<ImageSourceDetails>(manager.source("image")->details).runtime.loadError,
             QString("Load failed"));
    QVERIFY(manager.setSourceStatus("image", "error", "Load failed"));
    QCOMPARE(status.count(), 1);
    QVERIFY(manager.setSourceStatus("image", "ready", {}));
    QVERIFY(
        std::get<ImageSourceDetails>(manager.source("image")->details).runtime.loadError.isEmpty());
    QVERIFY(manager.setSourceStatus("camera", "error", "Device lost"));
    QCOMPARE(std::get<CameraSourceDetails>(manager.source("camera")->details).runtime.captureError,
             QString("Device lost"));
    QCOMPARE(changed.count(), 0); // Runtime notifications do not restart controllers.
    image.details = CameraSourceDetails{CameraConfig{"replacement"}};
    QVERIFY(!image.pixelSize().isValid());
    QVERIFY(std::get<CameraSourceDetails>(image.details).runtime.captureError.isEmpty());
}

void VideoSourceManagerTest::selectionNotifiesOnlyOnChange()
{
    VideoSourceManager state(sources());
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
    VideoSourceManager manager(sources());
    const QString id = manager.sources().at(row).id;
    const QString survivorId = manager.sources().at((row + 1) % 3).id;
    QVERIFY(manager.selectSource(id));
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    QSignalSpy changed(&manager, &VideoSourceManager::sourcesChanged);
    QVERIFY(manager.removeSource(id));
    QCOMPARE(manager.sources().size(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(selected.count(), 1);
    QVERIFY(manager.currentSourceId().isEmpty());
    QVERIFY(!manager.source(id));
    QVERIFY(manager.source(survivorId));
}

void VideoSourceManagerTest::removalNotifiesAfterMutation()
{
    VideoSourceManager manager(sources());
    QVERIFY(manager.selectSource("a"));
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    int notifications = 0;
    connect(&manager, &VideoSourceManager::sourcesChanged, this, [&] {
        QCOMPARE(manager.sources().size(), 2);
        QCOMPARE(manager.sources().at(1).id, QString("c"));
        QVERIFY(!manager.source("b"));
        ++notifications;
    });
    QVERIFY(manager.removeSource("b"));
    QCOMPARE(notifications, 1);
    QCOMPARE(manager.currentSourceId(), QString("a"));
    QCOMPARE(selected.count(), 0);
}

void VideoSourceManagerTest::movesSourcesAndPreservesIdentity()
{
    VideoSourceManager manager(sources());
    QVERIFY(manager.selectSource("a"));
    QVERIFY(!manager.canMoveCurrentSourceUp());
    QVERIFY(manager.canMoveCurrentSourceDown());
    QVERIFY(manager.setSourceVisible("a", false));
    QVERIFY(manager.setSourceLocked("a", true));
    QSignalSpy changed(&manager, &VideoSourceManager::sourcesChanged);
    QSignalSpy selected(&manager, &VideoSourceManager::currentSourceIdChanged);
    QSignalSpy available(&manager, &VideoSourceManager::sourceMoveAvailabilityChanged);
    QVERIFY(manager.moveSource("a", 1));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(available.count(), 1);
    QCOMPARE(selected.count(), 0);
    QCOMPARE(manager.sources().at(1).id, QString("a"));
    QCOMPARE(manager.source("a")->name, QString("Image"));
    QVERIFY(!manager.source("a")->visible);
    QVERIFY(manager.source("a")->locked);
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
    VideoSourceManager manager(sources());
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
    QVERIFY(!manager.source("b")->visible);
    QVERIFY(manager.source("b")->locked);
    QVERIFY(manager.removeSource("b"));
}

void VideoSourceManagerTest::rejectsInvalidOperations()
{
    VideoSourceManager state(sources());
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
    const QString base = QString::fromUtf8("图像");
    VideoSourceManager state(
        QList<VideoSource>{{"a", base}, {"b", base + " 3"}, {"c", "Camera"}, {"d", " cAmErA 2 "}});
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
        {"a", "First", ImageSourceDetails{ImageConfig{path}, ImageRuntime{image.size()}}},
        {"b", "Second", ImageSourceDetails{ImageConfig{path}, ImageRuntime{image.size()}}}};
    VideoSourceManager state(entries);
    entries.clear(); // State owns its source list.
    QVERIFY(state.setSourceVisible("a", false));
    QVERIFY(state.setSourceLocked("a", true));
    QVERIFY(state.moveSource("a", 1));
    const auto other = *state.source("b");
    QCOMPARE(other.id, QString("b"));
    QCOMPARE(other.pixelSize(), image.size());
    QVERIFY(other.visible);
    QVERIFY(!other.locked);
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
    QSignalSpy availability(&model, &VideoSourceManager::sourceMoveAvailabilityChanged);
    QVERIFY(!model.addSource({" ", "Invalid"}));
    QVERIFY(model.addSource({"one", "First", ImageSourceDetails{ImageConfig{"one.png"}}}));
    QVERIFY(!model.addSource({"one", "Duplicate"}));
    QVERIFY(model.addSource({"two", "Second", ImageSourceDetails{ImageConfig{"one.png"}}}));
    QCOMPARE(model.sources().size(), 2);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(model.currentSourceId(), QString("two"));
    QCOMPARE(model.sources().at(0).id, QString("two"));
    QVERIFY(model.canMoveCurrentSourceDown());
    QVERIFY(!model.canMoveCurrentSourceUp());
    QVERIFY(!availability.isEmpty());
    QVERIFY(model.setSourcePixelSize("two", QSize(40, 20)));
    QVERIFY(model.setSourcePixelSize("two", QSize(40, 20)));
    QCOMPARE(changed.count(), 1);
    QVERIFY(!model.setSourcePixelSize("missing", QSize(40, 20)));
}

void VideoSourceManagerTest::initializesAndValidatesTransforms()
{
    VideoSourceManager model;
    QVERIFY(model.addSource({"image", "Image", ImageSourceDetails{ImageConfig{"image.png"}}}));
    const auto current = [&] {
        return model.sources().at(0).transform;
    };
    QVERIFY(current().size.isEmpty());
    QVERIFY(model.setSourcePixelSize("image", QSize(4000, 2000)));
    QCOMPARE(current().center, QPointF(960, 540));
    QCOMPARE(current().size, QSizeF(1920, 960));
    VideoSourceTransform edited{{-20.5, 95.25}, {300.5, 100.25}, -30};
    QSignalSpy changes(&model, &VideoSourceManager::sourceChanged);
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(current().rotation, 330.0);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(changes.last().at(0).toString(), QString("image"));
    edited.rotation = 690;
    QVERIFY(model.setSourceTransform("image", edited));
    QCOMPARE(changes.size(), 1);
    QVERIFY(model.setSourcePixelSize("image", QSize(600, 200)));
    QCOMPARE(current().center, edited.center);
    QCOMPARE(current().size, edited.size);
    const VideoSourceTransform before = current();
    QVERIFY(!model.setSourceTransform("missing", before));
    QVERIFY(model.setSourceLocked("image", true));
    edited.center += QPointF(20, 20);
    QVERIFY(!model.setSourceTransform("image", edited));
    QVERIFY(model.setSourceLocked("image", false));
    for (int part = 0; part < 5; ++part) {
        auto invalid = before;
        if (part == 0)
            invalid.center.setX(std::numeric_limits<qreal>::infinity());
        if (part == 1)
            invalid.center.setY(std::numeric_limits<qreal>::quiet_NaN());
        if (part == 2)
            invalid.size.setWidth(.5);
        if (part == 3)
            invalid.size.setHeight(-1);
        if (part == 4)
            invalid.rotation = std::numeric_limits<qreal>::quiet_NaN();
        QVERIFY(!model.setSourceTransform("image", invalid));
        QVERIFY(current() == before);
    }
    VideoSourceManager ready(
        {{"ready", "Ready",
          ImageSourceDetails{ImageConfig{"ready.png"}, ImageRuntime{QSize(40, 20)}}}});
    QCOMPARE(ready.sources().at(0).transform.size, QSizeF(40, 20));
}

void VideoSourceManagerTest::sharesGeometryForMappingAndResizing()
{
    const auto thin = VideoSourceGeometry::initial(QSizeF(1, 4000));
    QVERIFY(VideoSourceGeometry::isValid(thin));
    QCOMPARE(thin.size.height(), 1080.0);
    QCOMPARE(thin.size.width(), .27);
    for (const QSizeF &widget : {QSizeF(1000, 700), QSizeF(300, 900), QSizeF(1600, 400)}) {
        const auto display = VideoSourceGeometry::canvasToWidget(widget);
        const QPointF point(135.25, 980.75);
        const auto roundTrip = display.inverted().map(display.map(point));
        QVERIFY(std::hypot(roundTrip.x() - point.x(), roundTrip.y() - point.y()) < 1e-8);
        QVERIFY(VideoSourceGeometry::canvasRect(widget).contains(display.map(point)));
    }
    for (qreal angle : {0., 37., 90., 225.}) {
        const VideoSourceTransform start{{960, 540}, {400, 200}, angle};
        const auto matrix = VideoSourceGeometry::sourceToCanvas(start);
        QVERIFY(VideoSourceGeometry::contains(start, matrix.map(QPointF(200, 100))));
        QVERIFY(VideoSourceGeometry::contains(start, matrix.map(QPointF(10, 10))));
        QVERIFY(!VideoSourceGeometry::contains(start, matrix.map(QPointF(-10, 100))));
        QTransform rotation;
        rotation.rotate(angle);
        for (int handle = 0; handle < 8; ++handle) {
            const auto result =
                VideoSourceGeometry::resized(start, handle, rotation.map(QPointF(40, 30)));
            QVERIFY(VideoSourceGeometry::isValid(result));
            const auto fraction = VideoSourceGeometry::handleFractions[handle];
            const QPointF opposite(1 - fraction.x(), 1 - fraction.y());
            const auto anchor = matrix.map(QPointF(opposite.x() * 400, opposite.y() * 200));
            const auto after = VideoSourceGeometry::sourceToCanvas(result).map(
                QPointF(opposite.x() * result.size.width(), opposite.y() * result.size.height()));
            QVERIFY(std::hypot(anchor.x() - after.x(), anchor.y() - after.y()) < 1e-8);
            if (handle % 2 == 0)
                QVERIFY(qAbs(result.size.width() / result.size.height() - 2) < 1e-8);
            else if (fraction.x() == .5)
                QCOMPARE(result.size.width(), 400.0);
            else
                QCOMPARE(result.size.height(), 200.0);
        }
        const auto crossed = VideoSourceGeometry::resized(start, 0, rotation.map(QPointF(10000, 10000)));
        QVERIFY(crossed.size.width() >= 1 && crossed.size.height() >= 1);
        const auto narrow = VideoSourceGeometry::resized(start, 3, rotation.map(QPointF(-10000, 0)));
        QCOMPARE(narrow.size.width(), 1.0);
    }
}

void VideoSourceManagerTest::filtersAreOrderedValidatedAndIndependent()
{
    VideoSourceManager model(
        QList<VideoSource>{{"a", "A", ImageSourceDetails{ImageConfig{"a.png"}}},
                           {"b", "B", ImageSourceDetails{ImageConfig{"a.png"}}},
                           {"camera", "Camera", CameraSourceDetails{CameraConfig{}}}});
    QSignalSpy changed(&model, &VideoSourceManager::sourceFiltersChanged);
    QSignalSpy data(&model, &VideoSourceManager::sourceChanged);
    QVERIFY(!model.canFilterCurrentSource());
    QVERIFY(model.selectSource("a"));
    QVERIFY(model.canFilterCurrentSource());
    QVERIFY(model.canFilterSource("camera"));
    QVERIFY(model.addFilter("missing").isEmpty());
    const QString first = model.addFilter("a");
    const QString second = model.addFilter("a");
    QVERIFY(!first.isEmpty() && first != second);
    QCOMPARE(changed.count(), 2);
    QCOMPARE(data.last().at(0).toString(), QString("a"));
    QCOMPARE(model.sourceFilters("a")[0].brightness, 0.f);
    QCOMPARE(model.sourceFilters("a")[0].contrast, 1.f);
    QCOMPARE(model.sourceFilters("a")[0].saturation, 1.f);
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
    QCOMPARE(model.sourceFilters("a")[1].id, first);
    QVERIFY(model.setFilterEnabled("a", first, true));
    QCOMPARE(model.sourceFilters("a")[1].brightness, .2f);
    const auto typed = model.sources().at(0).filters;
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

void VideoSourceManagerTest::displayPropertiesPreserveSourceState()
{
    VideoSourceManager model(QList<VideoSource>{
        {"screen", "Screen", DisplaySourceDetails{DisplayConfig{"physical-path", true}}},
        {"image", "Image", ImageSourceDetails{ImageConfig{"a.png"}}},
        {"camera", "Camera", CameraSourceDetails{CameraConfig{}}}});
    QSignalSpy changed(&model, &VideoSourceManager::sourceChanged);
    QSignalSpy status(&model, &VideoSourceManager::sourceStatusChanged);
    QVERIFY(model.selectSource("screen"));
    QVERIFY(model.canEditCurrentDisplay() && model.canFilterCurrentSource());
    QVERIFY(model.setSourcePixelSize("screen", QSize(1920, 1080)));
    const VideoSourceTransform transform{{300, 500}, {640, 360}, 45};
    QVERIFY(model.setSourceTransform("screen", transform));
    const auto filter = model.addFilter("screen");
    QVERIFY(model.setSourceLocked("screen", true));
    const int before = changed.count();
    QVERIFY(!model.setDisplayConfig("screen", " ", true));
    QVERIFY(!model.setDisplayConfig("image", "another", true));
    QVERIFY(!model.setDisplayConfig("missing", "another", true));
    QVERIFY(model.setDisplayConfig("screen", "physical-path", true));
    QCOMPARE(changed.count(), before);
    QVERIFY(model.setDisplayConfig("screen", "another-physical-path", false));
    QCOMPARE(changed.last().at(0).toString(), QString("screen"));
    QCOMPARE(model.sources().at(0).transform, transform);
    QCOMPARE(model.sourceFilters("screen")[0].id, filter);
    QCOMPARE(model.displaySource("screen")->config.displayId, QString("another-physical-path"));
    QVERIFY(!model.displaySource("screen")->config.captureCursor);
    QVERIFY(model.setSourceStatus("screen", "error", "Disconnected"));
    QCOMPARE(status.count(), 1);
    QVERIFY(model.setSourceStatus("screen", "error", "Disconnected"));
    QCOMPARE(status.count(), 1);
    QCOMPARE(std::get<DisplaySourceDetails>(model.sources().at(0).details).runtime.captureState,
             QString("error"));
    QCOMPARE(model.displaySource("screen")->runtime.captureError, QString("Disconnected"));
    QVERIFY(model.setSourceStatus("screen", "starting", {}));
    QVERIFY(model.displaySource("screen")->runtime.captureError.isEmpty());
    QVERIFY(model.selectSource("image"));
    QVERIFY(!model.canEditCurrentDisplay() && model.canFilterCurrentSource());
    QVERIFY(model.selectSource("camera"));
    QVERIFY(!model.canEditCurrentDisplay() && model.canFilterCurrentSource() &&
            model.canEditCurrentSource());
    QVERIFY(model.removeSource("screen"));
    QVERIFY(!model.displaySource("screen"));
    QVERIFY(!model.setSourceStatus("screen", "error", "Removed"));
}

void VideoSourceManagerTest::sourceRemovalDuringGeometryNotificationIsSafe()
{
    VideoSourceManager manager;
    QVERIFY(manager.addSource({"camera", "Camera", CameraSourceDetails{CameraConfig{"device"}}}));
    connect(&manager, &VideoSourceManager::sourceChanged, this, [&](const QString &sourceId) {
        manager.removeSource(sourceId);
    });
    manager.setCameraActualMode("camera", QSize(1280, 720), 30, 1);
    QVERIFY(manager.sources().isEmpty());
}
