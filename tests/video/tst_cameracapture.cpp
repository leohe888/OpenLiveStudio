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
    void configurationAndLayout();
    void unavailableDeviceAndLifecycle();
    void hardware();
};

void CameraCaptureTest::configurationAndLayout()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    const QVariantMap mode{
        {"width", 1280}, {"height", 720}, {"numerator", 30000}, {"denominator", 1001}};
    const QString id = controller.addCamera("Camera", "missing-device", mode);
    QVERIFY(!id.isEmpty());
    QVERIFY(controller.addCamera("Duplicate", "missing-device", {}).isEmpty());
    QCOMPARE(model.cameraProperties(id).value("denominator").toInt(), 1001);
    QVERIFY(model.canEditCurrentSource());
    QVERIFY(model.canFilterCurrentSource());
    model.setCameraActualMode(id, {1280, 720}, 30000, 1001);
    const SourceTransform transform{{440, 330}, {600, 337.5}, 35};
    QVERIFY(model.setSourceTransform(id, transform));
    QVERIFY(model.setSourceLocked(id, true));
    const QString filter = model.addColorCorrection(id);
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
    QSignalSpy errors(&controller, &CameraSourceController::sourceError);
    QList<PreviewSource> output;
    connect(&controller, &CameraSourceController::sourcesChanged, this, [&](const auto &sources) {
        output = sources;
    });
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(model.addSource(
        {"image", "Image",
         ImageSourceDetails{ImageConfig{"unused"}, ImageRuntimeState{image.size()}}}));
    const QString id = controller.addCamera("Camera", "missing-device", {});
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraProperties(id).value("state").toString(),
                              QString("error"), 10000);
    QVERIFY(!model.cameraProperties(id).value("error").toString().isEmpty());
    QVERIFY(!errors.isEmpty());
    QCOMPARE(output.size(), 1);
    QCOMPARE(output[0].id, id);
    QVERIFY(output[0].image.isNull());
    QVERIFY(model.setSourceVisible(id, false));
    QCOMPARE(model.cameraProperties(id).value("state").toString(), QString("paused"));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    const int before = errors.count();
    QVERIFY(model.setSourceVisible(id, true));
    QCOMPARE(model.cameraProperties(id).value("state").toString(), QString("error"));
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
    QHash<QString, QVariantList> catalogs;
    for (const auto &item : devices) {
        const auto device = item.toMap();
        const QString deviceId = device.value("id").toString();
        const auto modes = cameraModes(deviceId, &error);
        qInfo().noquote() << "MF device:" << device.value("name").toString()
                          << "modes:" << modes.size();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!modes.isEmpty());
        catalogs.insert(deviceId, modes);
        const QString id = controller.addCamera(device.value("name").toString(), deviceId, {});
        QVERIFY(!id.isEmpty());
        ids.append(id);
    }
    for (const auto &id : ids) {
        QTRY_VERIFY_WITH_TIMEOUT(
            model.cameraProperties(id).value("state").toString() == "capturing" ||
                model.cameraProperties(id).value("state").toString() == "error",
            15000);
        QVERIFY2(model.cameraProperties(id).value("state").toString() == "capturing",
                 qPrintable(model.cameraProperties(id).value("error").toString()));
        qInfo() << "Actual mode:" << model.cameraProperties(id);
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
        QTRY_COMPARE_WITH_TIMEOUT(model.cameraProperties(id).value("state").toString(),
                                  QString("capturing"), 15000);
    // Apply an explicit native mode after both devices have been opened and released.
    const auto deviceId = devices[0].toMap().value("id").toString();
    const auto modes = catalogs.value(deviceId);
    const auto mode = modes.last().toMap();
    QVERIFY(controller.applyProperties(ids[0], deviceId, mode));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraProperties(ids[0]).value("state").toString(),
                              QString("capturing"), 15000);
    QCOMPARE(model.cameraProperties(ids[0]).value("actualWidth").toInt(),
             mode.value("width").toInt());
    QCOMPARE(model.cameraProperties(ids[0]).value("actualHeight").toInt(),
             mode.value("height").toInt());
    // Unsupported explicit modes must fail while preserving the requested configuration.
    const QVariantMap unsupported{
        {"width", 9999}, {"height", 9999}, {"numerator", 30}, {"denominator", 1}};
    QVERIFY(controller.applyProperties(ids[0], deviceId, unsupported));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraProperties(ids[0]).value("state").toString(),
                              QString("error"), 15000);
    QCOMPARE(model.cameraProperties(ids[0]).value("width").toInt(), 9999);
    QVERIFY(controller.applyProperties(ids[0], deviceId, mode));
    QTRY_COMPARE_WITH_TIMEOUT(model.cameraProperties(ids[0]).value("state").toString(),
                              QString("capturing"), 15000);
    for (const auto &id : ids)
        QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}
QTEST_GUILESS_MAIN(CameraCaptureTest)
#include "tst_cameracapture.moc"
