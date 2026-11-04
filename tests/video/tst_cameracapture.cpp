#include "videosourcetesthelpers.h"
#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemanager.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}
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
    void configUpdatesPreserveTransformAndAspectRatio();
    void directShowFrameIntervalsPreserveStandardRates();
    void pixelFormatsParticipateInModeAndConfigIdentity();
    void preservesYuvPlanesStrideAndColorMetadata();
    void unavailableDeviceAndLifecycle();
    void capturesHardwareCameraAndSupportedModes();
};

void CameraCaptureTest::typedModesPreserveDefaultsAndFrameRates()
{
    const CameraMode deviceDefault;
    QVERIFY(!deviceDefault.isValid());
    const auto automatic = deviceDefault.config("camera");
    QVERIFY(automatic.isValid());
    QVERIFY(automatic.resolution().isEmpty());
    QCOMPARE(automatic.frameRateNumerator(), 0);

    const CameraMode selected{1280, 720, 30000, 1001};
    const CameraMode equivalent{1280, 720, 60000, 2002};
    const CameraMode different{1280, 720, 30, 1};
    QVERIFY(selected.isValid());
    QVERIFY(selected.sameFormat(equivalent));
    QVERIFY(!selected.sameFormat(different));
    const auto config = selected.config("camera");
    QCOMPARE(config.deviceId(), QString("camera"));
    QCOMPARE(config.resolution(), QSize(1280, 720));
    QCOMPARE(config.frameRateNumerator(), 30000);
    QCOMPARE(config.frameRateDenominator(), 1001);
    const CameraMode invalid{640, 480, 30, 0};
    QVERIFY(!invalid.isValid());
    QVERIFY(!invalid.config("camera").isValid());
}

void CameraCaptureTest::directShowFrameIntervalsPreserveStandardRates()
{
    QCOMPARE(cameraFrameRateFromInterval(333333), qMakePair(30, 1));
    QCOMPARE(cameraFrameRateFromInterval(333334), qMakePair(30, 1));
    QCOMPARE(cameraFrameRateFromInterval(333667), qMakePair(30000, 1001));
    QCOMPARE(cameraFrameRateFromInterval(166833), qMakePair(60000, 1001));
    QCOMPARE(cameraFrameRateFromInterval(1333333), qMakePair(15, 2));
    QCOMPARE(cameraFrameRateFromInterval(0), qMakePair(0, 1));
}

void CameraCaptureTest::pixelFormatsParticipateInModeAndConfigIdentity()
{
    CameraMode yuy2{1280, 720, 30, 1, "yuyv422"}, nv12{1280, 720, 30, 1, "nv12"};
    QVERIFY(!yuy2.sameFormat(nv12));
    QVERIFY(yuy2.config("camera") != nv12.config("camera"));
    QCOMPARE(yuy2.config("camera").pixelFormat(), QString("yuyv422"));
    CameraMode formatDefault;
    formatDefault.subtype = "mjpeg";
    const auto config = formatDefault.config("camera");
    QVERIFY(config.isValid());
    QVERIFY(config.resolution().isEmpty());
    QCOMPARE(config.pixelFormat(), QString("mjpeg"));
}

void CameraCaptureTest::preservesYuvPlanesStrideAndColorMetadata()
{
    AVFrame *frame = av_frame_alloc();
    QVERIFY(frame);
    frame->width = 6;
    frame->height = 4;
    frame->format = AV_PIX_FMT_NV12;
    QVERIFY(av_frame_get_buffer(frame, 32) >= 0);
    for (int y = 0; y < 4; ++y)
        memset(frame->data[0] + y * frame->linesize[0], 16 + y, 6);
    for (int y = 0; y < 2; ++y)
        memset(frame->data[1] + y * frame->linesize[1], 128 + y, 6);
    bool inferred = false;
    auto pixels = cameraPixels(frame, inferred);
    QVERIFY(pixels);
    QVERIFY(inferred);
    QVERIFY(!pixels->fullRange);
    QVERIFY(!pixels->bt709);
    QCOMPARE(pixels->planes[0].size(), 24);
    QCOMPARE(pixels->planes[1].size(), 12);
    QCOMPARE(pixels->planeSizes[1], QSize(3, 2));
    QCOMPARE(uchar(pixels->planes[0][18]), uchar(19));
    QCOMPARE(uchar(pixels->planes[1][6]), uchar(129));
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->colorspace = AVCOL_SPC_BT709;
    pixels = cameraPixels(frame, inferred);
    QVERIFY(!inferred);
    QVERIFY(pixels->fullRange);
    QVERIFY(pixels->bt709);
    frame->data[0] += 3 * frame->linesize[0];
    frame->linesize[0] = -frame->linesize[0];
    pixels = cameraPixels(frame, inferred);
    QVERIFY(pixels);
    QCOMPARE(uchar(pixels->planes[0][0]), uchar(19));
    QCOMPARE(uchar(pixels->planes[0][18]), uchar(16));
    av_frame_free(&frame);
    frame = av_frame_alloc();
    frame->width = 8;
    frame->height = 4;
    frame->format = AV_PIX_FMT_YUV420P10LE;
    QVERIFY(!cameraPixels(frame, inferred));
    av_frame_free(&frame);
}

void CameraCaptureTest::configUpdatesPreserveTransformAndAspectRatio()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    const CameraMode mode{1280, 720, 30000, 1001};
    const QString id = controller.addCamera("Camera", mode.config("missing-device"));
    QVERIFY(!id.isEmpty());
    QVERIFY(controller.addCamera("Duplicate", CameraSourceConfig{"missing-device"}).isEmpty());
    QCOMPARE(cameraSource(model, id)->frameRateDenominator(), 1001);
    QVERIFY(model.canFilterCurrentSource());
    setCameraActualMode(model, id, {1280, 720}, 30000, 1001);
    const VideoSourceTransform transform{{440, 330}, {600, 337.5}, 35};
    QVERIFY(model.setSourceTransform(id, transform));
    QVERIFY(model.setSourceLocked(id, true));
    const QString filter = model.addFilter(id);
    QVERIFY(!filter.isEmpty());
    setCameraActualMode(model, id, {640, 480}, 30, 1);
    const auto adjusted = model.sources().at(0).transform();
    QCOMPARE(adjusted.center(), transform.center());
    QCOMPARE(adjusted.rotationDegrees(), transform.rotationDegrees());
    QCOMPARE(adjusted.size(), QSizeF(600, 450));
    QCOMPARE(model.sourceFilters(id)[0].id(), filter);
    QVERIFY(!setCameraConfig(model, id, CameraSourceConfig{"missing-device", {1280, 720}, 30, 0}));
    QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}

void CameraCaptureTest::unavailableDeviceAndLifecycle()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    QSignalSpy errors(&controller, &CameraSourceController::sourceFailed);
    QList<RenderSourceSnapshot> output;
    connect(&controller, &CameraSourceController::renderSourcesChanged, this,
            [&](const auto &sources) {
                output = sources;
            });
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(model.addSource({"image", "Image", ImageSourceConfig{"unused"}}));
    const QString id = controller.addCamera("Camera", CameraSourceConfig{"missing-device"});
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceRuntime(id).state(), VideoSourceState::Error, 10000);
    QVERIFY(!model.sourceRuntime(id).error().isEmpty());
    QVERIFY(!errors.isEmpty());
    QCOMPARE(output.size(), 1);
    QCOMPARE(output[0].id, id);
    QVERIFY(output[0].image.isNull());
    QVERIFY(model.setSourceVisible(id, false));
    QCOMPARE(model.sourceRuntime(id).state(), VideoSourceState::Paused);
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    const int before = errors.count();
    QVERIFY(model.setSourceVisible(id, true));
    QCOMPARE(model.sourceRuntime(id).state(), VideoSourceState::Error);
    QCOMPARE(errors.count(), before);
    QCOMPARE(controller.activeWorkerCount(), 0);
    QVERIFY(controller.retry(id));
    QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    QVERIFY(output.isEmpty());
    QCOMPARE(model.sources().size(),
             1); // 图片仍属于管理器，但不应混入摄像头控制器的输出。
}

void CameraCaptureTest::capturesHardwareCameraAndSupportedModes()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_CAMERA_HARDWARE"))
        QSKIP("Set OPENLIVE_CAMERA_HARDWARE=1 to open cameras.");
    QString error;
    const auto devices = cameraDevices(&error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(!devices.isEmpty(), "No DirectShow cameras available");
    VideoSourceManager model;
    CameraSourceController controller(model);
    QStringList ids;
    QHash<QString, QList<CameraMode>> catalogs;
    for (const auto &item : devices) {
        const auto &device = item;
        const QString deviceId = device.id;
        const auto modes = cameraModes(deviceId, &error);
        qInfo().noquote() << "FFmpeg/DirectShow device:" << device.name << "modes:" << modes.size();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!modes.isEmpty());
        catalogs.insert(deviceId, modes);
        const QString id = controller.addCamera(device.name, CameraSourceConfig{deviceId});
        QVERIFY(!id.isEmpty());
        ids.append(id);
    }
    for (const auto &id : ids) {
        QTRY_VERIFY_WITH_TIMEOUT(model.sourceRuntime(id).state() == VideoSourceState::Capturing ||
                                     model.sourceRuntime(id).state() == VideoSourceState::Error,
                                 15000);
        QVERIFY2(model.sourceRuntime(id).state() == VideoSourceState::Capturing,
                 qPrintable(model.sourceRuntime(id).error()));
        qInfo() << "Actual mode:" << model.sourceRuntime(id).pixelSize();
    }
    QTest::qWait(2000);
    QElapsedTimer interval;
    interval.start();
    qint64 maxFrameGapMs = 0;
    int updates = 0;
    const auto countFrames = connect(&controller, &CameraSourceController::renderSourcesChanged,
                                     this, [&](const auto &) {
                                         maxFrameGapMs = qMax(maxFrameGapMs, interval.restart());
                                         ++updates;
                                     });
    const int testDurationMs =
        qMax(2000, qEnvironmentVariableIntValue("OPENLIVE_CAMERA_STRESS_MS"));
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX before{}, after{};
    before.cb = sizeof(before);
    after.cb = sizeof(after);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&before),
                         sizeof(before));
    DWORD handlesBefore = 0, handlesAfter = 0;
    GetProcessHandleCount(GetCurrentProcess(), &handlesBefore);
#endif
    QTest::qWait(testDurationMs);
    disconnect(countFrames);
    QVERIFY(updates > 0);
    QVERIFY2(maxFrameGapMs < 2000, "Camera publication stalled for over two seconds");
#ifdef Q_OS_WIN
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&after),
                         sizeof(after));
    GetProcessHandleCount(GetCurrentProcess(), &handlesAfter);
    qInfo() << "Stability duration ms:" << testDurationMs << "updates:" << updates
            << "max publication gap ms:" << maxFrameGapMs
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
        QTRY_COMPARE_WITH_TIMEOUT(model.sourceRuntime(id).state(), VideoSourceState::Capturing,
                                  15000);
    // 先打开并释放两个设备，再指定原生模式，验证模式切换不受旧会话影响。
    const auto deviceId = devices[0].id;
    const auto modes = catalogs.value(deviceId);
    const auto selected = std::find_if(modes.crbegin(), modes.crend(), [](const auto &mode) {
        return mode.supported;
    });
    QVERIFY(selected != modes.crend());
    const auto mode = *selected;
    qInfo() << "Selected mode:" << mode.name << mode.subtype;
    QVERIFY(model.setSourceConfig(ids[0], mode.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceRuntime(ids[0]).state(), VideoSourceState::Capturing,
                              15000);
    QCOMPARE(model.sourceRuntime(ids[0]).pixelSize().width(), mode.width);
    QCOMPARE(model.sourceRuntime(ids[0]).pixelSize().height(), mode.height);
    // 不支持的模式应报告失败并保留请求配置，便于用户修正。
    const CameraMode unsupported{9999, 9999, 30, 1};
    QVERIFY(model.setSourceConfig(ids[0], unsupported.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceRuntime(ids[0]).state(), VideoSourceState::Error, 15000);
    QCOMPARE(cameraSource(model, ids[0])->resolution().width(), 9999);
    QVERIFY(model.setSourceConfig(ids[0], mode.config(deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceRuntime(ids[0]).state(), VideoSourceState::Capturing,
                              15000);
    QSet<QString> checkedFormats;
    for (const auto &available : modes) {
        if (!available.supported || checkedFormats.contains(available.subtype))
            continue;
        const auto preferred = std::find_if(modes.cbegin(), modes.cend(), [&](const auto &m) {
            return m.subtype == available.subtype && m.width == 640 && m.height == 480 &&
                   m.frameRateNumerator == 30 && m.frameRateDenominator == 1;
        });
        const auto selectedMode = preferred == modes.cend() ? available : *preferred;
        QVERIFY(model.setSourceConfig(ids[0], selectedMode.config(deviceId)));
        QTRY_VERIFY_WITH_TIMEOUT(model.sourceRuntime(ids[0]).state() ==
                                         VideoSourceState::Capturing ||
                                     model.sourceRuntime(ids[0]).state() == VideoSourceState::Error,
                                 15000);
        QVERIFY2(model.sourceRuntime(ids[0]).state() == VideoSourceState::Capturing,
                 qPrintable(model.sourceRuntime(ids[0]).error()));
        QVERIFY(model.sourceRuntime(ids[0]).cameraStatus().startsWith(available.subtype));
        qInfo() << "Verified native format:" << model.sourceRuntime(ids[0]).cameraStatus();
        checkedFormats.insert(available.subtype);
    }
    for (const auto &id : ids)
        QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}
QTEST_GUILESS_MAIN(CameraCaptureTest)
#include "tst_cameracapture.moc"
