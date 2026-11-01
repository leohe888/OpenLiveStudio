#include "video/videosourceaggregator.h"
#include "video/videosourcemanager.h"

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

QStringList ids(const QList<PreviewSource> &sources)
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
    QList<PreviewSource> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::sourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty());

    QVERIFY(model.addSource({"image", "Image", ImageSourceConfig{"pending.png"}}));
    aggregator.setImageSources({PreviewSource{"image", {}}});
    QTRY_COMPARE(output.size(), 1);
    QVERIFY(!output[0].hasFrame()); // Loading/failed sources are not silently dropped.

    VideoSourceManager existing(mixedSources());
    VideoSourceAggregator initial(existing);
    QList<PreviewSource> initialOutput;
    connect(&initial, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        initialOutput = sources;
    });
    initial.setCameraSources({PreviewSource{"camera", image(Qt::blue)}});
    QTRY_COMPARE(ids(initialOutput), QStringList{"camera"});
}

void VideoSourceAggregatorTest::mergesInModelOrderAndPreservesFrames()
{
    VideoSourceManager model(mixedSources());
    VideoSourceAggregator aggregator(model);
    QList<PreviewSource> output;
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    auto frame = std::make_shared<CaptureFrame>();
    frame->size = {1920, 1080};
    frame->sequence = 42;
    frame->generation = 7;
    PreviewSource display;
    display.id = "display";
    display.frame = frame;
    const auto still = image(Qt::red), camera = image(Qt::blue);
    aggregator.setDisplaySources({display});
    aggregator.setImageSources({PreviewSource{"image", still}});
    aggregator.setCameraSources({PreviewSource{"camera", camera}});
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
    VideoSourceManager model(mixedSources());
    VideoSourceAggregator aggregator(model);
    QList<PreviewSource> output;
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    const auto red = image(Qt::red), blue = image(Qt::blue), green = image(Qt::green);
    aggregator.setImageSources({PreviewSource{"image", red}});
    aggregator.setDisplaySources({PreviewSource{"display", blue}});
    aggregator.setCameraSources({PreviewSource{"camera", blue}});
    QTRY_COMPARE(output.size(), 3);
    aggregator.setImageSources({PreviewSource{"image", green}});
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
    VideoSourceManager model(
        {{"image", "Image",
          ImageSourceConfig{"image.png"}}});
    model.setSourcePixelSize("image", {40, 20});
    VideoSourceAggregator aggregator(model);
    QList<PreviewSource> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::sourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    const auto pixels = image(Qt::red);
    aggregator.setImageSources({PreviewSource{"image", pixels}});
    QTRY_COMPARE(output.size(), 1);
    const VideoSourceTransform transform{{150, 100}, {80, 40}, 30};
    QVERIFY(model.setSourceTransform("image", transform));
    QVERIFY(model.setSourceVisible("image", false));
    QVERIFY(model.setSourceLocked("image", true));
    const auto filter = model.addFilter("image");
    QVERIFY(!filter.isEmpty());
    QVERIFY(model.setFilterParameters("image", filter, .2, .8, 1.1));
    QTRY_COMPARE(output[0].transform, transform);
    QVERIFY(!output[0].visible);
    QVERIFY(output[0].locked);
    QCOMPARE(output[0].filters.size(), 1);
    QCOMPARE(output[0].filters[0].id, filter);
    QCOMPARE(output[0].filters[0].brightness, .2f);
    QCOMPARE(output[0].image, pixels);

    changes.clear();
    QVERIFY(model.setSourceStatus("image", "error", "failed"));
    QCoreApplication::processEvents();
    QCOMPARE(changes.count(), 0); // Status text alone does not change the render snapshot.
}

void VideoSourceAggregatorTest::rejectsForeignAndMismatchedSources()
{
    auto sources = mixedSources();
    VideoSourceManager model(sources);
    VideoSourceAggregator aggregator(model);
    QList<PreviewSource> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::sourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &items) {
        output = items;
    });
    aggregator.setImageSources({PreviewSource{"camera", image(Qt::red)},
                                PreviewSource{"unknown", image(Qt::red)},
                                PreviewSource{"display", image(Qt::red)}});
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty());
    aggregator.setCameraSources({PreviewSource{"camera", image(Qt::blue)}});
    QTRY_COMPARE(ids(output), QStringList{"camera"});
    QCOMPARE(output[0].image, image(Qt::blue));
}

void VideoSourceAggregatorTest::removesSourcesAndReleasesFrames()
{
    VideoSourceManager model(mixedSources());
    VideoSourceAggregator aggregator(model);
    QList<PreviewSource> output;
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::sourcesChanged);
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    auto frame = std::make_shared<CaptureFrame>();
    std::weak_ptr<CaptureFrame> lease = frame;
    PreviewSource display;
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
    aggregator.setDisplaySources({PreviewSource{"display", image(Qt::red)}});
    QTRY_COMPARE(changes.count(), 1);
    QVERIFY(output.isEmpty()); // A late update cannot resurrect a removed source.
}

void VideoSourceAggregatorTest::coalescesUpdatesAndCancelsOnDestruction()
{
    VideoSourceManager model(mixedSources());
    VideoSourceAggregator aggregator(model);
    QSignalSpy changes(&aggregator, &VideoSourceAggregator::sourcesChanged);
    QList<PreviewSource> output;
    connect(&aggregator, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    aggregator.setImageSources({PreviewSource{"image", image(Qt::red)}});
    aggregator.setImageSources({PreviewSource{"image", image(Qt::green)}});
    aggregator.setCameraSources({PreviewSource{"camera", image(Qt::blue)}});
    QCOMPARE(changes.count(), 0);
    QTRY_COMPARE(changes.count(), 1);
    QCOMPARE(ids(output), (QStringList{"camera", "image"}));
    QCOMPARE(output[1].image, image(Qt::green));
    int afterDestruction = 0;
    {
        VideoSourceAggregator pending(model);
        connect(&pending, &VideoSourceAggregator::sourcesChanged, this, [&](const auto &) {
            ++afterDestruction;
        });
        pending.setImageSources({PreviewSource{"image", image(Qt::red)}});
    }
    QCoreApplication::processEvents();
    QCOMPARE(afterDestruction, 0);
}

QTEST_GUILESS_MAIN(VideoSourceAggregatorTest)
#include "tst_videosourceaggregator.moc"
