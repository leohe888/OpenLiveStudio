#include "videosourcetesthelpers.h"
#include "video/camera/v4l2camera.h"
#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemanager.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavcodec/avcodec.h>
}
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QtTest/QTest>

class CameraCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void typedModesPreserveDefaultsAndFrameRates();
    void workerOwnsCaptureSettingsSnapshot();
    void automaticModePrefersAspectRatioAndFrameRate();
    void rawFramesValidateStrideBoundsAndPlaneOrder();
    void captureSettingUpdatesPreserveTransformAndAspectRatio();
    void pixelFormatsParticipateInCaptureSettings();
    void preservesYuvPlanesStrideAndColorMetadata();
    void unavailableDeviceAndLifecycle();
    void capturesHardwareCameraAndSupportedModes();
};

void CameraCaptureTest::automaticModePrefersAspectRatioAndFrameRate()
{
    const QList<CameraMode> modes{
        {2560, 720, 30, 1, "mjpeg"}, {1280, 720, 30, 1, "mjpeg"},
        {1280, 720, 15, 2, "yuyv422"}, {640, 480, 30, 1, "yuyv422"}};
    const auto selected = preferredCameraMode(modes, cameraRequest("camera"));
    QCOMPARE(selected.width, 1280);
    QCOMPARE(selected.subtype, QString("mjpeg"));
    QCOMPARE(selected.frameRateNumerator, 30);
    const auto manual = cameraForMode(modes[3], "camera");
    QVERIFY(preferredCameraMode(modes, manual).sameFormat(modes[3]));
    QVERIFY(!preferredCameraMode(modes, cameraRequest("camera", {1920, 1080}, 30, 1)).isValid());
    QVERIFY(!preferredCameraMode({}, cameraRequest("camera")).isValid());
}

void CameraCaptureTest::rawFramesValidateStrideBoundsAndPlaneOrder()
{
    v4l2_pix_format format{};
    format.width = 4; format.height = 4;
    format.pixelformat = V4L2_PIX_FMT_YVU420;
    format.bytesperline = 8;
    format.quantization = V4L2_QUANTIZATION_LIM_RANGE;
    format.ycbcr_enc = V4L2_YCBCR_ENC_709;
    QByteArray bytes(48, char(0));
    memset(bytes.data(), 16, 32);
    memset(bytes.data() + 32, 200, 8); // V
    memset(bytes.data() + 40, 40, 8);  // U
    AVFrame frame{};
    cameraRawFrame(&frame, format, reinterpret_cast<unsigned char *>(bytes.data()), bytes.size());
    bool inferred = true;
    const auto pixels = cameraPixels(&frame, inferred);
    QVERIFY(pixels);
    QVERIFY(!inferred);
    QVERIFY(pixels->bt709);
    QCOMPARE(pixels->planes[0].size(), 16);
    QCOMPARE(uchar(pixels->planes[1][0]), uchar(40));
    QCOMPARE(uchar(pixels->planes[2][0]), uchar(200));
    QVERIFY_EXCEPTION_THROWN(cameraRawFrame(&frame, format,
        reinterpret_cast<unsigned char *>(bytes.data()), 43), QString);
    format.bytesperline = 2;
    QVERIFY_EXCEPTION_THROWN(cameraRawFrame(&frame, format,
        reinterpret_cast<unsigned char *>(bytes.data()), bytes.size()), QString);
    format.bytesperline = 8;
    format.pixelformat = V4L2_PIX_FMT_YUYV;
    cameraRawFrame(&frame, format, reinterpret_cast<unsigned char *>(bytes.data()), 32);
    QCOMPARE(frame.linesize[0], 8);
}

void CameraCaptureTest::typedModesPreserveDefaultsAndFrameRates()
{
    const CameraMode deviceDefault;
    QVERIFY(!deviceDefault.isValid());
    const auto automatic = cameraForMode(deviceDefault, "camera");
    QVERIFY(automatic.hasValidCaptureSettings());
    QVERIFY(automatic.resolution().isEmpty());
    QCOMPARE(automatic.frameRateNumerator(), 0);

    const CameraMode selected{1280, 720, 30000, 1001};
    const CameraMode equivalent{1280, 720, 60000, 2002};
    const CameraMode different{1280, 720, 30, 1};
    QVERIFY(selected.isValid());
    QVERIFY(selected.sameFormat(equivalent));
    QVERIFY(!selected.sameFormat(different));
    const auto config = cameraForMode(selected, "camera");
    QCOMPARE(config.deviceId(), QString("camera"));
    QCOMPARE(config.resolution(), QSize(1280, 720));
    QCOMPARE(config.frameRateNumerator(), 30000);
    QCOMPARE(config.frameRateDenominator(), 1001);
    const CameraMode invalid{640, 480, 30, 0};
    QVERIFY(!invalid.isValid());
    QVERIFY(!cameraForMode(invalid, "camera").hasValidCaptureSettings());
}

void CameraCaptureTest::pixelFormatsParticipateInCaptureSettings()
{
    CameraMode yuy2{1280, 720, 30, 1, "yuyv422"}, nv12{1280, 720, 30, 1, "nv12"};
    QVERIFY(!yuy2.sameFormat(nv12));
    QVERIFY(!cameraForMode(yuy2, "camera").hasSameCaptureSettings(cameraForMode(nv12, "camera")));
    QCOMPARE(cameraForMode(yuy2, "camera").pixelFormat(), QString("yuyv422"));
    CameraMode formatDefault;
    formatDefault.subtype = "mjpeg";
    const auto config = cameraForMode(formatDefault, "camera");
    QVERIFY(config.hasValidCaptureSettings());
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

void CameraCaptureTest::captureSettingUpdatesPreserveTransformAndAspectRatio()
{
    VideoSourceManager model;
    CameraSourceController controller(model);
    const CameraMode mode{1280, 720, 30000, 1001};
    const QString id = addCamera(controller, "Camera", cameraForMode(mode, "missing-device"));
    QVERIFY(!id.isEmpty());
    QVERIFY(addCamera(controller, "Duplicate", cameraRequest("missing-device")).isEmpty());
    QCOMPARE(cameraSource(model, id)->frameRateDenominator(), 1001);
    QVERIFY(model.canFilterCurrentSource());
    setCameraActualMode(model, id, {1280, 720}, 30000, 1001);
    const VideoSourceTransform transform{{440, 330}, {600, 337.5}, 35};
    QVERIFY(model.setSourceTransform(id, transform));
    QVERIFY(model.setSourceLocked(id, true));
    const QString filter = model.addFilter(id);
    QVERIFY(!filter.isEmpty());
    setCameraActualMode(model, id, {640, 480}, 30, 1);
    const auto adjusted = model.sources().at(0)->transform();
    QCOMPARE(adjusted.center(), transform.center());
    QCOMPARE(adjusted.rotationDegrees(), transform.rotationDegrees());
    QCOMPARE(adjusted.size(), QSizeF(600, 450));
    QCOMPARE(model.sourceFilters(id)[0].id(), filter);
    QVERIFY(!setCameraSettings(model, id, cameraRequest("missing-device", {1280, 720}, 30, 0)));
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
    QVERIFY(addSource(model, {"image", "Image", "unused"}));
    const QString id = addCamera(controller, "Camera", cameraRequest("missing-device"));
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(id), VideoSourceState::Error, 10000);
    QVERIFY(!model.sourceError(id).isEmpty());
    QVERIFY(!errors.isEmpty());
    const auto firstErrors = errors.count();
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > firstErrors, 6000); // 自动恢复，不依赖手动 retry。
    QCOMPARE(output.size(), 1);
    QCOMPARE(output[0].id, id);
    QVERIFY(output[0].image.isNull());
    QVERIFY(model.setSourceVisible(id, false));
    QCOMPARE(model.sourceState(id), VideoSourceState::Paused);
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    const int before = errors.count();
    QVERIFY(model.setSourceVisible(id, true));
    QTRY_VERIFY_WITH_TIMEOUT(errors.count() > before, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(id), VideoSourceState::Error, 5000);
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
    QVERIFY2(!devices.isEmpty(), "No V4L2 cameras available");
    VideoSourceManager model;
    CameraSourceController controller(model);
    QHash<QString, std::shared_ptr<const CaptureFrame>> latest;
    connect(&controller, &CameraSourceController::renderSourcesChanged, this,
            [&](const auto &sources) {
                latest.clear();
                for (const auto &source : sources) latest.insert(source.id, source.frame);
            });
    QStringList ids;
    QHash<QString, QList<CameraMode>> catalogs;
    for (const auto &item : devices) {
        const auto &device = item;
        const QString deviceId = device.id;
        const auto modes = cameraModes(deviceId, &error);
        qInfo().noquote() << "V4L2 device:" << device.name << "modes:" << modes.size();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(!modes.isEmpty());
        catalogs.insert(deviceId, modes);
        const QString id = addCamera(controller, device.name, cameraRequest(deviceId));
        QVERIFY(!id.isEmpty());
        ids.append(id);
    }
    for (const auto &id : ids) {
        QTRY_VERIFY_WITH_TIMEOUT(model.sourceState(id) == VideoSourceState::Capturing ||
                                     model.sourceState(id) == VideoSourceState::Error,
                                 15000);
        QVERIFY2(model.sourceState(id) == VideoSourceState::Capturing,
                 qPrintable(model.sourceError(id)));
        qInfo() << "Actual mode:" << model.sourcePixelSize(id);
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
    QTest::qWait(testDurationMs);
    disconnect(countFrames);
    QVERIFY(updates > 0);
    QVERIFY2(maxFrameGapMs < 2000, "Camera publication stalled for over two seconds");
    // 注入一次来源失败，验证最后画面保留、自动新会话和 generation 隔离。
    const auto frozen = latest.value(ids[0]);
    QVERIFY(frozen);
    controller.reportCaptureFailure(ids[0], frozen->generation, "Injected capture interruption");
    QCOMPARE(model.sourceState(ids[0]), VideoSourceState::Error);
    QCOMPARE(latest.value(ids[0]), frozen);
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(ids[0]), VideoSourceState::Capturing, 15000);
    QVERIFY(latest.value(ids[0]));
    QVERIFY(latest.value(ids[0])->generation > frozen->generation);
    QCOMPARE(controller.activeWorkerCount(), ids.size());
    for (const auto &id : ids)
        QVERIFY(model.setSourceVisible(id, false));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
    for (const auto &id : ids)
        QVERIFY(model.setSourceVisible(id, true));
    for (const auto &id : ids)
        QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(id), VideoSourceState::Capturing, 15000);
    // 先打开并释放两个设备，再指定原生模式，验证模式切换不受旧会话影响。
    const auto deviceId = devices[0].id;
    const auto modes = catalogs.value(deviceId);
    auto selected = std::find_if(modes.cbegin(), modes.cend(), [](const auto &mode) {
        return mode.supported && mode.width == 640 && mode.height == 480 &&
               mode.frameRateNumerator == 30 * mode.frameRateDenominator;
    });
    if (selected == modes.cend())
        selected = std::find_if(modes.cbegin(), modes.cend(), [](const auto &mode) { return mode.supported; });
    QVERIFY(selected != modes.cend());
    const auto mode = *selected;
    qInfo() << "Selected mode:" << mode.name << mode.subtype;
    QVERIFY(model.setCameraCaptureSettings(ids[0], cameraForMode(mode, deviceId)));
    QTRY_VERIFY_WITH_TIMEOUT(model.sourceState(ids[0]) == VideoSourceState::Capturing ||
                                 model.sourceState(ids[0]) == VideoSourceState::Error,
                             15000);
    QVERIFY2(model.sourceState(ids[0]) == VideoSourceState::Capturing,
             qPrintable(model.sourceError(ids[0])));
    QCOMPARE(model.sourcePixelSize(ids[0]).width(), mode.width);
    QCOMPARE(model.sourcePixelSize(ids[0]).height(), mode.height);
    // 不支持的模式应报告失败并保留请求配置，便于用户修正。
    const CameraMode unsupported{9999, 9999, 30, 1};
    QVERIFY(model.setCameraCaptureSettings(ids[0], cameraForMode(unsupported, deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(ids[0]), VideoSourceState::Error, 15000);
    QCOMPARE(cameraSource(model, ids[0])->resolution().width(), 9999);
    QVERIFY(model.setCameraCaptureSettings(ids[0], cameraForMode(mode, deviceId)));
    QTRY_COMPARE_WITH_TIMEOUT(model.sourceState(ids[0]), VideoSourceState::Capturing, 15000);
    QSet<QString> checkedFormats;
    for (const auto &available : modes) {
        if (!available.supported || checkedFormats.contains(available.subtype))
            continue;
        const auto preferred = std::find_if(modes.cbegin(), modes.cend(), [&](const auto &m) {
            return m.subtype == available.subtype && m.width == 640 && m.height == 480 &&
                   m.frameRateNumerator == 30 && m.frameRateDenominator == 1;
        });
        const auto selectedMode = preferred == modes.cend() ? available : *preferred;
        QVERIFY(model.setCameraCaptureSettings(ids[0], cameraForMode(selectedMode, deviceId)));
        QTRY_VERIFY_WITH_TIMEOUT(model.sourceState(ids[0]) == VideoSourceState::Capturing ||
                                     model.sourceState(ids[0]) == VideoSourceState::Error,
                                 15000);
        QVERIFY2(model.sourceState(ids[0]) == VideoSourceState::Capturing,
                 qPrintable(model.sourceError(ids[0])));
        QVERIFY(cameraSource(model, ids[0])->captureDescription().startsWith(available.subtype));
        qInfo() << "Verified native format:" << cameraSource(model, ids[0])->captureDescription();
        checkedFormats.insert(available.subtype);
    }
    for (const auto &id : ids)
        QVERIFY(model.removeSource(id));
    QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 10000);
}
QTEST_GUILESS_MAIN(CameraCaptureTest)
#include "tst_cameracapture.moc"

void CameraCaptureTest::workerOwnsCaptureSettingsSnapshot()
{
    CameraSource source{"camera", "Camera", "original-device", {640, 480}, 30, 1, "mjpeg"};
    CameraCaptureWorker worker(source, 1);
    source.setDeviceId("another-device");
    source.setPixelFormat("yuyv422");
    QCOMPARE(worker.deviceId(), QString("original-device"));
    QVERIFY(!worker.isRunning());
}
