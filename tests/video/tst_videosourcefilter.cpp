#include "video/videosourcefilter.h"

#include <QtTest/QTest>
#include <cmath>
#include <limits>

class VideoSourceFilterTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultsAndType();
    void configValidation_data();
    void configValidation();
    void identityAndComparison();
};

void VideoSourceFilterTest::defaultsAndType()
{
    VideoSourceFilter filter;
    QVERIFY(filter.id.isEmpty());
    QVERIFY(filter.enabled);
    QVERIFY(std::holds_alternative<ColorCorrectionConfig>(filter.config));
    QCOMPARE(filter.type(), VideoSourceFilterType::ColorCorrection);
    const auto &config = std::get<ColorCorrectionConfig>(filter.config);
    QCOMPARE(config.brightness, 0.f);
    QCOMPARE(config.contrast, 1.f);
    QCOMPARE(config.saturation, 1.f);
    QVERIFY(config.isValid());
    QVERIFY(config.isIdentity());

    filter.config = ColorCorrectionConfig{.2f, .5f, 2};
    filter.enabled = false;
    QCOMPARE(filter.type(), VideoSourceFilterType::ColorCorrection);
}

void VideoSourceFilterTest::configValidation_data()
{
    QTest::addColumn<float>("brightness");
    QTest::addColumn<float>("contrast");
    QTest::addColumn<float>("saturation");
    QTest::addColumn<bool>("valid");

    QTest::newRow("default") << 0.f << 1.f << 1.f << true;
    QTest::newRow("minimum") << -1.f << 0.f << 0.f << true;
    QTest::newRow("maximum") << 1.f << 2.f << 2.f << true;
    QTest::newRow("interior") << .25f << .5f << 1.5f << true;
    QTest::newRow("brightness-below-minimum")
        << std::nextafter(-1.f, -2.f) << 1.f << 1.f << false;
    QTest::newRow("brightness-above-maximum")
        << std::nextafter(1.f, 2.f) << 1.f << 1.f << false;
    QTest::newRow("contrast-below-minimum")
        << 0.f << std::nextafter(0.f, -1.f) << 1.f << false;
    QTest::newRow("contrast-above-maximum")
        << 0.f << std::nextafter(2.f, 3.f) << 1.f << false;
    QTest::newRow("saturation-below-minimum")
        << 0.f << 1.f << std::nextafter(0.f, -1.f) << false;
    QTest::newRow("saturation-above-maximum")
        << 0.f << 1.f << std::nextafter(2.f, 3.f) << false;

    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float invalid : {infinity, -infinity, nan}) {
        const auto suffix = QString::number(invalid).toLatin1();
        QTest::newRow(("brightness-" + suffix).constData()) << invalid << 1.f << 1.f << false;
        QTest::newRow(("contrast-" + suffix).constData()) << 0.f << invalid << 1.f << false;
        QTest::newRow(("saturation-" + suffix).constData()) << 0.f << 1.f << invalid << false;
    }
}

void VideoSourceFilterTest::configValidation()
{
    QFETCH(float, brightness);
    QFETCH(float, contrast);
    QFETCH(float, saturation);
    QFETCH(bool, valid);

    const ColorCorrectionConfig config{brightness, contrast, saturation};
    QCOMPARE(config.isValid(), valid);
}

void VideoSourceFilterTest::identityAndComparison()
{
    const ColorCorrectionConfig identity;
    QVERIFY(identity.isIdentity());
    QVERIFY(identity == ColorCorrectionConfig{});

    const ColorCorrectionConfig changed[] = {
        {std::nextafter(0.f, 1.f), 1, 1},
        {0, std::nextafter(1.f, 2.f), 1},
        {0, 1, std::nextafter(1.f, 2.f)},
    };
    for (const auto &config : changed) {
        QVERIFY(!config.isIdentity());
        QVERIFY(!(identity == config));
        QVERIFY(!(config == identity));
        const auto copy = config;
        QVERIFY(config == copy);
    }
}

QTEST_GUILESS_MAIN(VideoSourceFilterTest)
#include "tst_videosourcefilter.moc"
