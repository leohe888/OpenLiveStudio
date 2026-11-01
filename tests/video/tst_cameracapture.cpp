#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemanager.h"
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QtTest/QTest>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

class CameraCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void typedModesPreserveDefaultsAndFrameRates();
    void configurationAndLayout();
    void unavailableDeviceAndLifecycle();
    void hardware();
};

void CameraCaptureTest::typedModesPreserveDefaultsAndFrameRates()
{
    const CameraMode deviceDefault;
    QVERIFY(!deviceDefault.isValid());
    const auto automatic = deviceDefault.config("camera");
    QVERIFY(automatic.isValid());
    QVERIFY(automatic.requestedResolution.isEmpty());
    QCOMPARE(automatic.requestedFrameRateNumerator, 0);

    const CameraMode selected{1280, 720, 30000, 1001};
    const CameraMode equivalent{1280, 720, 60000, 2002};
    const CameraMode different{1280, 720, 30, 1};
    QVERIFY(selected.isValid());
    QVERIFY(selected.sameFormat(equivalent));
    QVERIFY(!selected.sameFormat(different));
    const auto config = selected.config("camera");
    QCOMPARE(config.deviceId, QString("camera"));
    QCOMPARE(config.requestedResolution, QSize(1280, 720));
    QCOMPARE(config.requestedFrameRateNumerator, 30000);
    QCOMPARE(config.requestedFrameRateDenominator, 1001);
    const CameraMode invalid{640, 480, 30, 0};
    QVERIFY(!invalid.isValid());
    QVERIFY(!invalid.config("camera").isValid());
}

void CameraCaptureTest::configurationAndLayout()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    const CameraMode mode{1280, 720, 30000, 1001};
    const QString id = controller.addCamera("Camera", mode.config("missing-device"));
    QVERIFY(!id.isEmpty());
    QVERIFY(controller.addCamera("Duplicate", CameraConfig{"missing-device"}).isEmpty());
    QCOMPARE(model.cameraSource(id)->config.requestedFrameRateDenominator, 1001);
    QVERIFY(model.canEditCurrentSource());
    QVERIFY(model.canFilterCurrentSource());
    model.setCameraActualMode(id, {1280, 720}, 30000, 1001);
    const SourceTransform transform{{440, 330}, {600, 337.5}, 35};
    QVERIFY(model.setSourceTransform(id, transform));
    QVERIFY(model.setSourceLocked(id, true));
    const QString filter = model.addFilter(id);
    QVERIFY(!filter.isEmpty());
    model.setCameraActualMode(id, {640, 480}, 30, 1);
    const auto adjusted = model.sources().at(0).transform;
    QCOMPARE(adjusted.center, transform.center);
    QCOMPARE(adjusted.rotation, transform.rotation);
    QCOMPARE(adjusted.size, QSizeF(600, 450));
    QCOMPARE(model.sourceFilters(id)[0].id, filter);
    QVERIFY(!model.setCameraConfig(id, CameraConfig{"missing-device", {1280, 720}, 30, 0}));
    QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}

void CameraCaptureTest::unavailableDeviceAndLifecycle()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    QSignalSpy errors(&controller, &CameraSourceController::sourceFailed);
    QList<PreviewSource> output;
    connect(&controller, &CameraSourceController::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(model.addSource(
        {"image", "Image",
         ImageSourceDetails{ImageConfig{"unused"}, ImageRuntimeState{image.size()}}}));
    const QString id = controller.addCamera("Camera", CameraConfig{"missing-device"});
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraSource(id)->runtime.captureState, QString("error"),
                              10000);
    QVERIFY(!model.cameraSource(id)->runtime.captureError.isEmpty());
    QVERIFY(!errors.isEmpty());
    QCOMPARE(output.size(), 1);
    QCOMPARE(output[0].id, id);
    QVERIFY(output[0].image.isNull());
    QVERIFY(model.setSourceVisible(id, false));
    QCOMPARE(model.cameraSource(id)->runtime.captureState, QString("paused"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    const int before = errors.count();
    QVERIFY(model.setSourceVisible(id, true));
    QCOMPARE(model.cameraSource(id)->runtime.captureState, QString("error"));
    QCOMPARE(errors.count(), before);
    QCOMPARE(controller.activeWorkerCount(), 0);
    QVERIFY(controller.retry(id));
    QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    QVERIFY(output.isEmpty());
    QCOMPARE(model.sources().size(),
             1); // The image remains in the model, not in this controller's output.
}

void CameraCaptureTest::hardware()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_CAMERA_HARDWARE"))
        QSKIP("Set OPENLIVE_CAMERA_HARDWARE=1 to open cameras.");
    QString error;
    const auto devices = cameraDevices(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(!devices.isEmpty(), "No Media Foundation cameras available");
    VideoSourceManager model;
    CameraSourceController controller(model);
    QStringList ids;
    QHash<QString, QList<CameraMode>> catalogs;
    for (const auto &item : devices) {
        const auto &device = item;
        const QString deviceId = device.id;
        const auto modes = cameraModes(deviceId, &error);
        qInfo().noquote() << "MF device:" << device.name << "modes:" << modes.size();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!modes.isEmpty());
        catalogs.insert(deviceId, modes);
        const QString id = controller.addCamera(device.name, CameraConfig{deviceId});
        QVERIFY(!id.isEmpty());
        ids.append(id);
    }
    for (const auto &id : ids) {
        QTRY_VERIFY_WITH_TIMEOUT(model.cameraSource(id)->runtime.captureState == "capturing" ||
                                     model.cameraSource(id)->runtime.captureState == "error",
                                 15000);
        QVERIFY2(model.cameraSource(id)->runtime.captureState == "capturing",
                 qPrintable(model.cameraSource(id)->runtime.captureError));
        qInfo() << "Actual mode:" << model.cameraSource(id)->runtime.pixelSize;
    }
    QTest::qWait(2000);
    QElapsedTimer interval;
    interval.start();
    qint64 maxGap = 0;
    int updates = 0;
    const auto countFrames =
        connect(&controller, &CameraSourceController::sourcesChanged, this, [&](const auto &) {
            maxGap = qMax(maxGap, interval.restart());
            ++updates;
        });
    const int duration = qMax(2000, qEnvironmentVariableIntValue("OPENLIVE_CAMERA_STRESS_MS"));
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX before{}, after{};
    before.cb = sizeof(before);
    after.cb = sizeof(after);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&before),
                         sizeof(before));
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
#endif
    QTest::qWait(duration);
    disconnect(countFrames);
    QVERIFY(updates > 0);
    QVERIFY2(maxGap < 2000, "Camera publication stalled for over two seconds");
#ifdef Q_OS_WIN
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&after),
                         sizeof(after));
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    qInfo() << "Stability duration ms:" << duration << "updates:" << updates
            << "max publication gap ms:" << maxGap
            << "private bytes:" << quint64(before.PrivateUsage) << quint64(after.PrivateUsage)
            << "handles:" << handlesBefore << handlesAfter;
    QVERIFY(after.PrivateUsage < before.PrivateUsage + 64 * 1024 * 1024);
    QVERIFY(handlesAfter <= handlesBefore + 16);
#endif
    QCOMPARE(controller.activeWorkerCount(), ids.size());
    for (const auto &id : ids)
        QVERIFY(model.setSourceVisible(id, false));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    for (const auto &id : ids)
        QVERIFY(model.setSourceVisible(id, true));
    for (const auto &id : ids)
        QTRY_COMPARE_WITH_TIMEOUT(model.cameraSource(id)->runtime.captureState,
                                  QString("capturing"), 15000);
    // Apply an explicit native mode after both devices have been opened and released.
    const auto deviceId = devices[0].id;
    const auto modes = catalogs.value(deviceId);
    const auto mode = modes.last();
    QVERIFY(controller.applyProperties(ids[0], mode.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraSource(ids[0])->runtime.captureState,
                              QString("capturing"), 15000);
    QCOMPARE(model.cameraSource(ids[0])->runtime.pixelSize.width(), mode.width);
    QCOMPARE(model.cameraSource(ids[0])->runtime.pixelSize.height(), mode.height);
    // Unsupported explicit modes must fail while preserving the requested configuration.
    const CameraMode unsupported{9999, 9999, 30, 1};
    QVERIFY(controller.applyProperties(ids[0], unsupported.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraSource(ids[0])->runtime.captureState, QString("error"),
                              15000);
    QCOMPARE(model.cameraSource(ids[0])->config.requestedResolution.width(), 9999);
    QVERIFY(controller.applyProperties(ids[0], mode.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraSource(ids[0])->runtime.captureState,
                              QString("capturing"), 15000);
    for (const auto &id : ids)
        QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}
QTEST_GUILESS_MAIN(CameraCaptureTest)
#include "tst_cameracapture.moc"
