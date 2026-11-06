#include "videosourcetesthelpers.h"

#include <QtTest/QTest>
#include <limits>

class VideoSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void shortConstructorsPreserveMemberDefaults();
    void concreteTypesOwnTheirConfig();
    void captureSettingEditsAndSourceCopiesAreIndependent();
    void transformConstructionAndAngles();
    void cameraSettingsValidation_data();
    void cameraSettingsValidation();
    void cameraSettingsComparison();
};

void VideoSourceTest::concreteTypesOwnTheirConfig()
{
    static_assert(std::is_abstract_v<VideoSource>);
    static_assert(std::has_virtual_destructor_v<VideoSource>);
    static_assert(!std::is_convertible_v<ImageSource, CameraSource>);
    DisplaySource display{"display", "Display", false};
    CameraSource camera{"camera", "Camera", "device"};
    ImageSource image{"image", "Image", "image.png"};
    const VideoSource *sources[]{&display, &camera, &image};
    QVERIFY(dynamic_cast<const DisplaySource *>(sources[0]));
    QVERIFY(dynamic_cast<const CameraSource *>(sources[1]));
    QVERIFY(dynamic_cast<const ImageSource *>(sources[2]));
    QVERIFY(!display.captureCursor());
    QCOMPARE(camera.deviceId(), QString("device"));
    QCOMPARE(image.filePath(), QString("image.png"));
    camera.setDeviceId("another-device");
    QCOMPARE(camera.deviceId(), QString("another-device"));
    camera.updateCapture({1280, 720}, 30000, 1001, "MJPEG");
    QCOMPARE(sources[1]->pixelSize(), QSize(1280, 720));
    QCOMPARE(sources[1]->state(), VideoSourceState::Capturing);
    QCOMPARE(camera.captureDescription(), QString("MJPEG"));
    camera.updateStatus(camera.pixelSize(), VideoSourceState::Error, "Disconnected");
    QCOMPARE(camera.error(), QString("Disconnected"));
    QCOMPARE(camera.pixelSize(), QSize(1280, 720));
    camera.updateCapture({640, 480}, 30, 1, "YUYV");
    QVERIFY(camera.error().isEmpty());
    QCOMPARE(camera.captureDescription(), QString("YUYV"));
}

void VideoSourceTest::transformConstructionAndAngles()
{
    const VideoSourceTransform unset;
    QVERIFY(!unset.isValid());
    QVERIFY(!unset.contains(QPointF(960, 540)));
    QCOMPARE(unset.center(), QPointF(960, 540));
    QCOMPARE(unset.rotationDegrees(), 0.0);
    for (const QSizeF &size : {QSizeF(), QSizeF(0, 100), QSizeF(-1, 100)})
        QVERIFY(VideoSourceTransform(size) == unset);
    QCOMPARE(VideoSourceTransform(QSizeF(400, 200)).size(), QSizeF(400, 200));
    QCOMPARE(VideoSourceTransform(QSizeF(4000, 2000)).size(), QSizeF(1920, 960));
    QCOMPARE(VideoSourceTransform(QSizeF(1000, 4000)).size(), QSizeF(270, 1080));
    const VideoSourceTransform explicitGeometry{{10, 20}, {100, 50}};
    QCOMPARE(explicitGeometry.rotationDegrees(), 0.0);
    QVERIFY(explicitGeometry.isValid());
    QVERIFY(explicitGeometry.contains(explicitGeometry.center()));
    QVERIFY(explicitGeometry != unset);
    VideoSourceTransform editable;
    editable.setCenter(QPointF(-20, 75));
    editable.setSize(QSizeF(300, 150));
    QCOMPARE(editable.center(), QPointF(-20, 75));
    QCOMPARE(editable.size(), QSizeF(300, 150));
    QVERIFY(editable.isValid());
    const qreal inputs[] = {-30, 690, 360, -720, 12.5};
    const qreal expected[] = {330, 330, 0, 0, 12.5};
    for (int i = 0; i < 5; ++i) {
        editable.setRotationDegrees(inputs[i]);
        QCOMPARE(editable.rotationDegrees(), expected[i]);
        const VideoSourceTransform constructed{editable.center(), editable.size(), inputs[i]};
        QVERIFY(editable == constructed);
        QCOMPARE(editable.sourceToCanvas(), constructed.sourceToCanvas());
    }
    for (qreal invalid :
         {std::numeric_limits<qreal>::infinity(), -std::numeric_limits<qreal>::infinity(),
          std::numeric_limits<qreal>::quiet_NaN()}) {
        editable.setRotationDegrees(invalid);
        QVERIFY(!editable.isValid());
        QVERIFY(!VideoSourceTransform(QPointF(), QSizeF(100, 50), invalid).isValid());
    }
    editable.setRotationDegrees(0);
    QVERIFY(editable.isValid());
}

void VideoSourceTest::cameraSettingsValidation_data()
{
    QTest::addColumn<QString>("deviceId");
    QTest::addColumn<QSize>("resolution");
    QTest::addColumn<int>("frameRateNumerator");
    QTest::addColumn<int>("frameRateDenominator");
    QTest::addColumn<bool>("valid");

    QTest::newRow("automatic") << QString("camera") << QSize() << 0 << 1 << true;
    QTest::newRow("explicit") << QString("camera") << QSize(1280, 720) << 30 << 1 << true;
    QTest::newRow("fractional-rate")
        << QString("camera") << QSize(1920, 1080) << 30000 << 1001 << true;
    QTest::newRow("missing-device") << QString() << QSize() << 0 << 1 << false;
    QTest::newRow("blank-device") << QString(" \t ") << QSize() << 0 << 1 << false;
    QTest::newRow("zero-denominator") << QString("camera") << QSize() << 0 << 0 << false;
    QTest::newRow("negative-denominator") << QString("camera") << QSize() << 0 << -1 << false;
    QTest::newRow("resolution-without-rate")
        << QString("camera") << QSize(1280, 720) << 0 << 1 << false;
    QTest::newRow("rate-without-resolution") << QString("camera") << QSize() << 30 << 1 << false;
    QTest::newRow("empty-resolution-with-rate")
        << QString("camera") << QSize(0, 720) << 30 << 1 << false;
    QTest::newRow("negative-rate") << QString("camera") << QSize(1280, 720) << -30 << 1 << false;
}

void VideoSourceTest::cameraSettingsValidation()
{
    QFETCH(QString, deviceId);
    QFETCH(QSize, resolution);
    QFETCH(int, frameRateNumerator);
    QFETCH(int, frameRateDenominator);
    QFETCH(bool, valid);

    const CameraSource config{
        {}, {}, deviceId, resolution, frameRateNumerator, frameRateDenominator};
    QCOMPARE(config.hasValidCaptureSettings(), valid);
}

void VideoSourceTest::cameraSettingsComparison()
{
    const CameraSource config{{}, {}, "camera", {1280, 720}, 30000, 1001};
    const auto copy = config;
    QVERIFY(config.hasSameCaptureSettings(copy));

    const CameraSource different[] = {
        {{}, {}, "another-camera", {1280, 720}, 30000, 1001},
        {{}, {}, "camera", {1920, 1080}, 30000, 1001},
        {{}, {}, "camera", {1280, 720}, 60000, 1001},
        {{}, {}, "camera", {1280, 720}, 30000, 2002},
    };
    for (const auto &other : different) {
        QVERIFY(!config.hasSameCaptureSettings(other));
        QVERIFY(!other.hasSameCaptureSettings(config));
    }
}

void VideoSourceTest::captureSettingEditsAndSourceCopiesAreIndependent()
{
    CameraSource camera{{}, {}, {}};
    QVERIFY(!camera.hasValidCaptureSettings());
    camera.setDeviceId("camera");
    QVERIFY(camera.hasValidCaptureSettings());
    camera.setResolution(QSize(1280, 720));
    QVERIFY(!camera.hasValidCaptureSettings());
    camera.setFrameRateNumerator(30000);
    camera.setFrameRateDenominator(1001);
    QVERIFY(camera.hasValidCaptureSettings());
    QVERIFY(camera.hasSameCaptureSettings(cameraRequest("camera", {1280, 720}, 30000, 1001)));

    CameraSource original{"source",
                          "Original",
                          camera.deviceId(),
                          camera.resolution(),
                          camera.frameRateNumerator(),
                          camera.frameRateDenominator()};
    original.updateCapture({1280, 720}, 30000, 1001, "MJPEG");
    original.setFilters({VideoSourceFilter{"filter"}});
    auto copy = original;
    copy.updateStatus(copy.pixelSize(), VideoSourceState::Error, "Disconnected");
    auto filters = copy.filters();
    filters[0].setEnabled(false);
    copy.setFilters(filters);
    copy.setName("Copy");
    copy.setDeviceId("other-device");
    copy.setVisible(false);
    copy.setLocked(true);
    copy.setTransform(VideoSourceTransform(QSizeF(640, 360)));
    QCOMPARE(original.deviceId(), QString("camera"));
    QCOMPARE(original.captureDescription(), QString("MJPEG"));
    QCOMPARE(original.id(), QString("source"));
    QCOMPARE(original.name(), QString("Original"));
    QCOMPARE(original.state(), VideoSourceState::Capturing);
    QVERIFY(original.error().isEmpty());
    QVERIFY(original.filters()[0].enabled());
    QVERIFY(original.visible());
    QVERIFY(!original.locked());
    QVERIFY(original.transform().size().isEmpty());
}

void VideoSourceTest::shortConstructorsPreserveMemberDefaults()
{
    DisplaySource display{{}, {}};
    const DisplaySource constructedDisplay{{}, {}};
    QCOMPARE(constructedDisplay.captureCursor(), display.captureCursor());

    CameraSource camera{{}, {}, {}};
    camera.setDeviceId("camera");
    QVERIFY(camera.hasSameCaptureSettings(cameraRequest("camera")));

    CameraSource automatic{{}, {}, "camera"};
    QCOMPARE(automatic.state(), VideoSourceState::Idle);
    QCOMPARE(automatic.negotiatedFrameRateNumerator(), 0);
    QCOMPARE(automatic.negotiatedFrameRateDenominator(), 1);
    QVERIFY(automatic.captureDescription().isEmpty());

    const DisplaySource source{"source", "Source"};
    QCOMPARE(source.id(), QString("source"));
    QCOMPARE(source.name(), QString("Source"));
    QVERIFY(source.visible());
    QVERIFY(!source.locked());
    QVERIFY(source.transform().size().isEmpty());
    QVERIFY(source.filters().isEmpty());
    QVERIFY(source.pixelSize().isEmpty());
    const ImageSource image{"image", "Image", "image.png"};
    QVERIFY(image.visible());
    QVERIFY(!image.locked());
}

QTEST_GUILESS_MAIN(VideoSourceTest)
#include "tst_videosource.moc"
