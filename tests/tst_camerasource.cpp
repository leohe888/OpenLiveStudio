#include "camerasource.h"

#include <QCameraDevice>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <memory>

class CameraSourceTest : public QObject
{
    Q_OBJECT
private slots:
    void startsStoppedAndStopIsIdempotent();
    void enumeratesOpaqueDeviceIds();
    void rejectsMissingDevicesAndCanReset();
    void parentOwnsSource();
    void capturesRealDeviceWhenEnabled();
};

void CameraSourceTest::startsStoppedAndStopIsIdempotent()
{
    CameraSource source;
    QCOMPARE(source.state(), CameraSource::State::Stopped);
    QVERIFY(source.errorString().isEmpty());
    QVERIFY(source.deviceId().isEmpty());
    QVERIFY(!source.frameSize().isValid());
    QSignalSpy changed(&source, &CameraSource::statusChanged);
    QSignalSpy frames(&source, &CameraSource::frameReady);
    source.stop();
    source.stop();
    QCOMPARE(changed.count(), 0);
    QCOMPARE(frames.count(), 0);
}

void CameraSourceTest::enumeratesOpaqueDeviceIds()
{
    QSet<QString> ids;
    const auto devices = CameraSource::availableDevices();
    for (const auto &device : devices) {
        QVERIFY(!device.id.isEmpty());
        QVERIFY(!ids.contains(device.id));
        ids.insert(device.id);
        bool found = false;
        for (const auto &native : QMediaDevices::videoInputs()) {
            if (native.id() == QByteArray::fromBase64(device.id.toLatin1(), QByteArray::Base64UrlEncoding)) {
                QCOMPARE(device.description, native.description());
                QCOMPARE(device.isDefault, native.isDefault());
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }
    qInfo() << "Available camera devices:" << devices.size();
}

void CameraSourceTest::rejectsMissingDevicesAndCanReset()
{
    CameraSource source;
    QSignalSpy frames(&source, &CameraSource::frameReady);
    for (const auto &id : {QString(), QStringLiteral("!missing-camera")}) {
        QVERIFY(!source.start(id));
        QCOMPARE(source.state(), CameraSource::State::Error);
        QVERIFY(!source.errorString().isEmpty());
        QVERIFY(!source.frameSize().isValid());
        QTest::qWait(20);
        QCOMPARE(frames.count(), 0);
        source.stop();
        QCOMPARE(source.state(), CameraSource::State::Stopped);
        QVERIFY(source.errorString().isEmpty());
        QVERIFY(source.deviceId().isEmpty());
    }
}

void CameraSourceTest::parentOwnsSource()
{
    auto parent = std::make_unique<QObject>();
    QPointer<CameraSource> source = new CameraSource(parent.get());
    QVERIFY(!source->start(QStringLiteral("!missing-camera")));
    parent.reset();
    QVERIFY(source.isNull());
}

void CameraSourceTest::capturesRealDeviceWhenEnabled()
{
    if (qEnvironmentVariableIntValue("OPENLIVE_STUDIO_TEST_CAMERA") != 1)
        QSKIP("Set OPENLIVE_STUDIO_TEST_CAMERA=1 to explicitly enable real camera capture.");
    const auto devices = CameraSource::availableDevices();
    if (devices.isEmpty())
        QSKIP("No camera device is available; real capture is unverified.");
    auto device = devices.first();
    for (const auto &candidate : devices) {
        if (candidate.isDefault)
            device = candidate;
    }
    qInfo() << "Capturing camera:" << device.description;
    CameraSource source;
    int frames = 0;
    connect(&source, &CameraSource::frameReady, this, [&](const QVideoFrame &frame) {
        QVERIFY(frame.isValid());
        QVERIFY(!frame.size().isEmpty());
        QCOMPARE(source.state(), CameraSource::State::Capturing);
        ++frames;
    });
    QVERIFY2(source.start(device.id), qPrintable(source.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(frames >= 3 || source.state() == CameraSource::State::Error, 15000);
    QVERIFY2(frames >= 3, qPrintable(source.errorString()));
    QVERIFY(source.frameSize().isValid());
    QVERIFY(source.start(device.id)); // Repeated start keeps the same live session.
    source.stop();
    const int stoppedAt = frames;
    QTest::qWait(250);
    QCOMPARE(frames, stoppedAt);
    QCOMPARE(source.state(), CameraSource::State::Stopped);
    QVERIFY(!source.frameSize().isValid());
}

QTEST_MAIN(CameraSourceTest)
#include "tst_camerasource.moc"
