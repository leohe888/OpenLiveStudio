#include "video/videosource.h"

#include <QtTest/QTest>
#include <limits>

class VideoSourceTest : public QObject
{
    Q_OBJECT

private slots:
    void typeFollowsParameters();
    void canvasGeometryConstructionAndAngles();
    void cameraParametersValidation_data();
    void cameraParametersValidation();
    void cameraParametersComparison();
};

void VideoSourceTest::typeFollowsParameters()
{
    VideoSource source;
    QCOMPARE(source.type(), VideoSourceType::DisplayCapture);
    source.config = CameraSourceParams{"camera"};
    QCOMPARE(source.type(), VideoSourceType::Camera);
    source.config = ImageSourceParams{"image.png"};
    QCOMPARE(source.type(), VideoSourceType::Image);
    source.config = DisplaySourceParams{"display", false};
    QCOMPARE(source.type(), VideoSourceType::DisplayCapture);
}

void VideoSourceTest::canvasGeometryConstructionAndAngles()
{
    const VideoSourceCanvasGeometry unset;
    QVERIFY(!unset.isValid());
    QVERIFY(!unset.contains(QPointF(960, 540)));
    QCOMPARE(unset.center(), QPointF(960, 540));
    QCOMPARE(unset.rotation(), 0.0);
    for (const QSizeF &size : {QSizeF(), QSizeF(0, 100), QSizeF(-1, 100)})
        QVERIFY(VideoSourceCanvasGeometry(size) == unset);
    QCOMPARE(VideoSourceCanvasGeometry(QSizeF(400, 200)).size(), QSizeF(400, 200));
    QCOMPARE(VideoSourceCanvasGeometry(QSizeF(4000, 2000)).size(), QSizeF(1920, 960));
    QCOMPARE(VideoSourceCanvasGeometry(QSizeF(1000, 4000)).size(), QSizeF(270, 1080));
    const VideoSourceCanvasGeometry explicitGeometry{{10, 20}, {100, 50}};
    QCOMPARE(explicitGeometry.rotation(), 0.0);
    QVERIFY(explicitGeometry.isValid());
    QVERIFY(explicitGeometry.contains(explicitGeometry.center()));
    QVERIFY(explicitGeometry != unset);
    VideoSourceCanvasGeometry editable;
    editable.setCenter(QPointF(-20, 75));
    editable.setSize(QSizeF(300, 150));
    QCOMPARE(editable.center(), QPointF(-20, 75));
    QCOMPARE(editable.size(), QSizeF(300, 150));
    QVERIFY(editable.isValid());
    const qreal inputs[] = {-30, 690, 360, -720, 12.5};
    const qreal expected[] = {330, 330, 0, 0, 12.5};
    for (int i = 0; i < 5; ++i) {
        editable.setRotation(inputs[i]);
        QCOMPARE(editable.rotation(), expected[i]);
        const VideoSourceCanvasGeometry constructed{editable.center(), editable.size(), inputs[i]};
        QVERIFY(editable == constructed);
        QCOMPARE(editable.sourceToCanvas(), constructed.sourceToCanvas());
    }
    for (qreal invalid : {std::numeric_limits<qreal>::infinity(),
                          -std::numeric_limits<qreal>::infinity(),
                          std::numeric_limits<qreal>::quiet_NaN()}) {
        editable.setRotation(invalid);
        QVERIFY(!editable.isValid());
        QVERIFY(!VideoSourceCanvasGeometry(QPointF(), QSizeF(100, 50), invalid).isValid());
    }
    editable.setRotation(0);
    QVERIFY(editable.isValid());
}

void VideoSourceTest::cameraParametersValidation_data()
{
    QTest::addColumn<QString>("deviceId");
    QTest::addColumn<QSize>("resolution");
    QTest::addColumn<int>("numerator");
    QTest::addColumn<int>("denominator");
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
    QTest::newRow("rate-without-resolution")
        << QString("camera") << QSize() << 30 << 1 << false;
    QTest::newRow("empty-resolution-with-rate")
        << QString("camera") << QSize(0, 720) << 30 << 1 << false;
    QTest::newRow("negative-rate")
        << QString("camera") << QSize(1280, 720) << -30 << 1 << false;
}

void VideoSourceTest::cameraParametersValidation()
{
    QFETCH(QString, deviceId);
    QFETCH(QSize, resolution);
    QFETCH(int, numerator);
    QFETCH(int, denominator);
    QFETCH(bool, valid);

    const CameraSourceParams params{deviceId, resolution, numerator, denominator};
    QCOMPARE(params.isValid(), valid);
}

void VideoSourceTest::cameraParametersComparison()
{
    const CameraSourceParams params{"camera", {1280, 720}, 30000, 1001};
    const auto copy = params;
    QVERIFY(params == copy);
    QVERIFY(!(params != copy));

    const CameraSourceParams different[] = {
        {"another-camera", {1280, 720}, 30000, 1001},
        {"camera", {1920, 1080}, 30000, 1001},
        {"camera", {1280, 720}, 60000, 1001},
        {"camera", {1280, 720}, 30000, 2002},
    };
    for (const auto &other : different) {
        QVERIFY(!(params == other));
        QVERIFY(!(other == params));
        QVERIFY(params != other);
        QVERIFY(other != params);
    }
}

QTEST_GUILESS_MAIN(VideoSourceTest)
#include "tst_videosource.moc"
