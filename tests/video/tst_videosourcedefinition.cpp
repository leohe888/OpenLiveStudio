#include "video/videosourcedefinition.h"

#include <QtTest/QTest>

class VideoSourceDefinitionTest : public QObject
{
    Q_OBJECT

private slots:
    void typeFollowsConfiguration();
    void cameraConfigurationValidation_data();
    void cameraConfigurationValidation();
    void cameraConfigurationComparison();
};

void VideoSourceDefinitionTest::typeFollowsConfiguration()
{
    VideoSourceDefinition source;
    QCOMPARE(source.type(), VideoSourceType::DisplayCapture);
    source.config = CameraSourceConfig{"camera"};
    QCOMPARE(source.type(), VideoSourceType::Camera);
    source.config = ImageSourceConfig{"image.png"};
    QCOMPARE(source.type(), VideoSourceType::Image);
    source.config = DisplaySourceConfig{"display", false};
    QCOMPARE(source.type(), VideoSourceType::DisplayCapture);
}

void VideoSourceDefinitionTest::cameraConfigurationValidation_data()
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

void VideoSourceDefinitionTest::cameraConfigurationValidation()
{
    QFETCH(QString, deviceId);
    QFETCH(QSize, resolution);
    QFETCH(int, numerator);
    QFETCH(int, denominator);
    QFETCH(bool, valid);

    const CameraSourceConfig config{deviceId, resolution, numerator, denominator};
    QCOMPARE(config.isValid(), valid);
}

void VideoSourceDefinitionTest::cameraConfigurationComparison()
{
    const CameraSourceConfig config{"camera", {1280, 720}, 30000, 1001};
    const auto copy = config;
    QVERIFY(config == copy);
    QVERIFY(!(config != copy));

    const CameraSourceConfig different[] = {
        {"another-camera", {1280, 720}, 30000, 1001},
        {"camera", {1920, 1080}, 30000, 1001},
        {"camera", {1280, 720}, 60000, 1001},
        {"camera", {1280, 720}, 30000, 2002},
    };
    for (const auto &other : different) {
        QVERIFY(!(config == other));
        QVERIFY(!(other == config));
        QVERIFY(config != other);
        QVERIFY(other != config);
    }
}

QTEST_GUILESS_MAIN(VideoSourceDefinitionTest)
#include "tst_videosourcedefinition.moc"
