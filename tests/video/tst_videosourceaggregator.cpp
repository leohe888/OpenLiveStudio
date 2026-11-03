#include "video/videosourceaggregator.h"
#include "video/videosourcemanager.h"
#include "videosourcetesthelpers.h"

#include <QSignalSpy>
#include <QtTest/QTest>

namespace {
QList<VideoSource> mixedSources()
{
    return {{"camera", "Camera", CameraSourceConfig{"device"}},
            {"image", "Image", ImageSourceConfig{"image.png"}},
            {"display", "Display", DisplaySourceConfig{"monitor"}}};
}

QImage image(Qt::GlobalColor color)
{
    QImage result(40, 20, QImage::Format_RGB32);
    result.fill(color);
    return result;
}

QStringList ids(const QList<RenderSourceSnapshot> &sources)
{
    QStringList result;
    for (const auto &source : sources)
        result.append(source.id);
    return result;
}
}

class VideoSourceAggregatorTest : public QObject
{
    Q_OBJECT
private slots:
    void publishesInitialAndPendingSnapshots();
    void mergesInModelOrderAndPreservesFrames();
    void replacesOnlyTheUpdatedType();
    void usesModelPropertiesWithoutNewFrames();
    void rejectsForeignAndMismatchedSources();
    void removesSourcesAndReleasesFrames();
    void coalescesUpdatesAndCancelsOnDestruction();
};

void VideoSourceAggregatorTest::publishesInitialAndPendingSnapshots()
{
    VideoSourceManager model;
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::renderSourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty());

    QVERIFY(model.addSource({"image", "Image", ImageSourceConfig{"pending.png"}}));
    aggregator.setImageSources({RenderSourceSnapshot{"image", {}}});
    QTRY_COMPARE(output.size(), 1);
    QVERIFY(!output[0].hasFrame()); // Loading/failed sources are not silently dropped.

    VideoSourceManager existing;
    QVERIFY(addSources(existing, mixedSources()));
    VideoSourceAggregator initial(existing);
    QList<RenderSourceSnapshot> initialOutput;
    connect(&initial, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        initialOutput = sources;
    });
    initial.setCameraSources({RenderSourceSnapshot{"camera", image(Qt::blue)}});
    QTRY_COMPARE(ids(initialOutput), QStringList{"camera"});
}

void VideoSourceAggregatorTest::mergesInModelOrderAndPreservesFrames()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, mixedSources()));
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    auto frame = std::make_shared<CaptureFrame>();
    frame->size = {1920, 1080};
    frame->sequence = 42;
    frame->generation = 7;
    RenderSourceSnapshot display;
    display.id = "display";
    display.frame = frame;
    const auto still = image(Qt::red), camera = image(Qt::blue);
    aggregator.setDisplaySources({display});
    aggregator.setImageSources({RenderSourceSnapshot{"image", still}});
    aggregator.setCameraSources({RenderSourceSnapshot{"camera", camera}});
    QTRY_COMPARE(ids(output), (QStringList{"camera", "image", "display"}));
    QCOMPARE(output[0].image, camera);
    QCOMPARE(output[1].image, still);
    QCOMPARE(output[2].frame.get(), frame.get());
    QCOMPARE(output[2].frame->generation, quint64(7));

    QVERIFY(model.moveSource("display", -1));
    QTRY_COMPARE(ids(output), (QStringList{"camera", "display", "image"}));
    QCOMPARE(output[1].frame.get(), frame.get());
}

void VideoSourceAggregatorTest::replacesOnlyTheUpdatedType()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, mixedSources()));
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    const auto red = image(Qt::red), blue = image(Qt::blue), green = image(Qt::green);
    aggregator.setImageSources({RenderSourceSnapshot{"image", red}});
    aggregator.setDisplaySources({RenderSourceSnapshot{"display", blue}});
    aggregator.setCameraSources({RenderSourceSnapshot{"camera", blue}});
    QTRY_COMPARE(output.size(), 3);
    aggregator.setImageSources({RenderSourceSnapshot{"image", green}});
    QTRY_COMPARE(output[1].image, green);
    QCOMPARE(output[0].image, blue);
    QCOMPARE(output[2].image, blue);
    aggregator.setDisplaySources({});
    QTRY_COMPARE(ids(output), (QStringList{"camera", "image"}));
    aggregator.setImageSources({});
    QTRY_COMPARE(ids(output), QStringList{"camera"});
    aggregator.setCameraSources({});
    QTRY_VERIFY(output.isEmpty());
}

void VideoSourceAggregatorTest::usesModelPropertiesWithoutNewFrames()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, {{"image", "Image",
          ImageSourceConfig{"image.png"}}}));
    setSourcePixelSize(model, "image", {40, 20});
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::renderSourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    const auto pixels = image(Qt::red);
    aggregator.setImageSources({RenderSourceSnapshot{"image", pixels}});
    QTRY_COMPARE(output.size(), 1);
    const VideoSourceTransform transform{{150, 100}, {80, 40}, 30};
    QVERIFY(model.setSourceTransform("image", transform));
    QVERIFY(model.setSourceVisible("image", false));
    QVERIFY(model.setSourceLocked("image", true));
    const auto filter = model.addFilter("image");
    QVERIFY(!filter.isEmpty());
    QVERIFY(setFilterConfig(model, "image", filter, ColorCorrectionConfig{.2f, .8f, 1.1f}));
    QTRY_COMPARE(output[0].transform, transform);
    QVERIFY(!output[0].visible);
    QVERIFY(output[0].locked);
    QCOMPARE(output[0].filters.size(), 1);
    QCOMPARE(output[0].filters[0].id(), filter);
    QCOMPARE(std::get<ColorCorrectionConfig>(output[0].filters[0].config()).brightness(), .2f);
    QCOMPARE(output[0].image, pixels);

    changes.clear();
    QVERIFY(setSourceStatus(model, "image", VideoSourceState::Error, "failed"));
    QCoreApplication::processEvents();
    QCOMPARE(changes.count(), 0); // Status text alone does not change the render snapshot.
}

void VideoSourceAggregatorTest::rejectsForeignAndMismatchedSources()
{
    auto sources = mixedSources();
    VideoSourceManager model;
    QVERIFY(addSources(model, sources));
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::renderSourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &items) {
        output = items;
    });
    aggregator.setImageSources({RenderSourceSnapshot{"camera", image(Qt::red)},
                                RenderSourceSnapshot{"unknown", image(Qt::red)},
                                RenderSourceSnapshot{"display", image(Qt::red)}});
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty());
    aggregator.setCameraSources({RenderSourceSnapshot{"camera", image(Qt::blue)}});
    QTRY_COMPARE(ids(output), QStringList{"camera"});
    QCOMPARE(output[0].image, image(Qt::blue));
}

void VideoSourceAggregatorTest::removesSourcesAndReleasesFrames()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, mixedSources()));
    VideoSourceAggregator aggregator(model);
    QList<RenderSourceSnapshot> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::renderSourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    auto frame = std::make_shared<CaptureFrame>();
    std::weak_ptr<CaptureFrame> lease = frame;
    RenderSourceSnapshot display;
    display.id = "display";
    display.frame = frame;
    aggregator.setDisplaySources({display});
    display.frame.reset();
    frame.reset();
    QTRY_COMPARE(output.size(), 1);
    changes.clear(); // QSignalSpy also retains the frame snapshot.
    QVERIFY(model.removeSource("display"));
    QTRY_VERIFY(output.isEmpty());
    QVERIFY(lease.expired());
    changes.clear();
    aggregator.setDisplaySources({RenderSourceSnapshot{"display", image(Qt::red)}});
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty()); // A late update cannot resurrect a removed source.
}

void VideoSourceAggregatorTest::coalescesUpdatesAndCancelsOnDestruction()
{
    VideoSourceManager model;
    QVERIFY(addSources(model, mixedSources()));
    VideoSourceAggregator aggregator(model);
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::renderSourcesChanged);
    QList<RenderSourceSnapshot> output;
    connect(&aggregator, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    aggregator.setImageSources({RenderSourceSnapshot{"image", image(Qt::red)}});
    aggregator.setImageSources({RenderSourceSnapshot{"image", image(Qt::green)}});
    aggregator.setCameraSources({RenderSourceSnapshot{"camera", image(Qt::blue)}});
    QCOMPARE(changes.count(), 0);
    QTRY_COMPARE(changes.count(), 1);
    QCOMPARE(ids(output), (QStringList{"camera", "image"}));
    QCOMPARE(output[1].image, image(Qt::green));
    int afterDestruction = 0;
    {
        VideoSourceAggregator pending(model);
        connect(&pending, &VideoSourceAggregator::renderSourcesChanged, this, [&](const auto &) {
            ++afterDestruction;
        });
        pending.setImageSources({RenderSourceSnapshot{"image", image(Qt::red)}});
    }
    QCoreApplication::processEvents();
    QCOMPARE(afterDestruction, 0);
}

QTEST_GUILESS_MAIN(VideoSourceAggregatorTest)
#include "tst_videosourceaggregator.moc"
