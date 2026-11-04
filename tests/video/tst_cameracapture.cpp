#include "videosourcetesthelpers.h"
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
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <mfapi.h>
#include <wrl/client.h>
#endif

class CameraCaptureTest : public QObject
{
    Q_OBJECT
private slots:
    void typedModesPreserveDefaultsAndFrameRates();
    void configUpdatesPreserveTransformAndAspectRatio();
    void mediaFoundationSamplesPreserveLayout();
    void mediaFoundationCompressedSamplesPreserveTimingAndHeaders();
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

void CameraCaptureTest::mediaFoundationSamplesPreserveLayout()
{
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    QVERIFY(SUCCEEDED(MFStartup(MF_VERSION)));
    const auto shutdown = qScopeGuard([] {
        MFShutdown();
    });
    ComPtr<IMFMediaType> type;
    QVERIFY(SUCCEEDED(MFCreateMediaType(&type)));
    QVERIFY(SUCCEEDED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)));
    QVERIFY(SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12)));
    QVERIFY(SUCCEEDED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 4, 4)));
    QVERIFY(SUCCEEDED(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30000, 1001)));
    QVERIFY(SUCCEEDED(type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709)));
    QVERIFY(SUCCEEDED(type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235)));
    ComPtr<IMFMediaBuffer> buffer;
    QVERIFY(SUCCEEDED(MFCreate2DMediaBuffer(4, 4, MFVideoFormat_NV12.Data1, FALSE, &buffer)));
    ComPtr<IMF2DBuffer2> plane;
    QVERIFY(SUCCEEDED(buffer.As(&plane)));
    BYTE *data = nullptr, *start = nullptr;
    LONG pitch = 0;
    DWORD length = 0;
    QVERIFY(
        SUCCEEDED(plane->Lock2DSize(MF2DBuffer_LockFlags_Write, &data, &pitch, &start, &length)));
    QVERIFY(pitch >= 4);
    for (int y = 0; y < 4; ++y)
        memset(data + y * pitch, 20 + y, 4);
    for (int y = 0; y < 2; ++y)
        memset(data + (4 + y) * pitch, 130 + y, 4);
    QVERIFY(SUCCEEDED(plane->Unlock2D()));
    ComPtr<IMFSample> sample;
    QVERIFY(SUCCEEDED(MFCreateSample(&sample)));
    QVERIFY(SUCCEEDED(sample->AddBuffer(buffer.Get())));
    bool inferred = true;
    auto pixels = cameraMfPixels(sample.Get(), type.Get(), inferred);
    QVERIFY(pixels);
    QVERIFY(!inferred);
    QVERIFY(pixels->bt709);
    QVERIFY(!pixels->fullRange);
    QCOMPARE(pixels->planes[0].size(), 16);
    QCOMPARE(uchar(pixels->planes[0][12]), uchar(23));
    QCOMPARE(uchar(pixels->planes[1][4]), uchar(131));

    // YV12 的物理 V/U 顺序必须转换为着色器约定的 Y/U/V 顺序。
    QVERIFY(SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_YV12)));
    QVERIFY(SUCCEEDED(type->SetUINT32(MF_MT_DEFAULT_STRIDE, 4)));
    buffer.Reset();
    QVERIFY(SUCCEEDED(MFCreateMemoryBuffer(24, &buffer)));
    QVERIFY(SUCCEEDED(buffer->Lock(&data, nullptr, nullptr)));
    memset(data, 16, 16);
    memset(data + 16, 200, 4);
    memset(data + 20, 40, 4);
    buffer->Unlock();
    QVERIFY(SUCCEEDED(buffer->SetCurrentLength(24)));
    sample->RemoveAllBuffers();
    sample->AddBuffer(buffer.Get());
    pixels = cameraMfPixels(sample.Get(), type.Get(), inferred);
    QCOMPARE(uchar(pixels->planes[1][0]), uchar(40));
    QCOMPARE(uchar(pixels->planes[2][0]), uchar(200));
    QVERIFY(SUCCEEDED(buffer->SetCurrentLength(23)));
    QVERIFY_EXCEPTION_THROWN(cameraMfPixels(sample.Get(), type.Get(), inferred), HRESULT);

    // 负 stride 的 RGB 缓冲保持显示方向，且 BGR0 的空字节不会作为透明度。
    QVERIFY(SUCCEEDED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)));
    QVERIFY(SUCCEEDED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 2, 2)));
    QVERIFY(SUCCEEDED(type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(LONG(-8)))));
    QVERIFY(SUCCEEDED(buffer->Lock(&data, nullptr, nullptr)));
    memset(data, 30, 8);
    memset(data + 8, 60, 8);
    buffer->Unlock();
    buffer->SetCurrentLength(16);
    pixels = cameraMfPixels(sample.Get(), type.Get(), inferred);
    QCOMPARE(uchar(pixels->planes[0][0]), uchar(60));
    QCOMPARE(uchar(pixels->planes[0][8]), uchar(30));
    QCOMPARE(uchar(pixels->planes[0][3]), uchar(255));
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_P010);
    QVERIFY_EXCEPTION_THROWN(cameraMfPixels(sample.Get(), type.Get(), inferred), HRESULT);
#else
    QSKIP("Media Foundation requires Windows");
#endif
}

void CameraCaptureTest::mediaFoundationCompressedSamplesPreserveTimingAndHeaders()
{
#ifdef Q_OS_WIN
    using Microsoft::WRL::ComPtr;
    QVERIFY(SUCCEEDED(MFStartup(MF_VERSION)));
    const auto shutdown = qScopeGuard([] {
        MFShutdown();
    });
    ComPtr<IMFMediaType> type;
    QVERIFY(SUCCEEDED(MFCreateMediaType(&type)));
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, 640, 480);
    MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, 30000, 1001);
    type->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    type->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    const auto headers = QByteArray::fromHex("000000016742001e0000000168ce06e2");
    QVERIFY(SUCCEEDED(type->SetBlob(MF_MT_MPEG_SEQUENCE_HEADER,
                                    reinterpret_cast<const UINT8 *>(headers.constData()),
                                    UINT32(headers.size()))));
    AVCodecParameters *parameters = avcodec_parameters_alloc();
    AVPacket *packet = av_packet_alloc();
    QVERIFY(parameters && packet);
    const auto freeCodec = qScopeGuard([&] {
        avcodec_parameters_free(&parameters);
        av_packet_free(&packet);
    });
    cameraMfParameters(type.Get(), parameters);
    QCOMPARE(parameters->codec_id, AV_CODEC_ID_H264);
    QCOMPARE(parameters->width, 640);
    QCOMPARE(parameters->height, 480);
    QCOMPARE(parameters->color_space, AVCOL_SPC_BT709);
    QCOMPARE(parameters->color_range, AVCOL_RANGE_MPEG);
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(parameters->extradata),
                        parameters->extradata_size),
             headers);
    for (int i = 0; i < AV_INPUT_BUFFER_PADDING_SIZE; ++i)
        QCOMPARE(parameters->extradata[parameters->extradata_size + i], uint8_t(0));
    const auto bytes = QByteArray::fromHex("0000000165881234000001411234");
    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buffer;
    QVERIFY(SUCCEEDED(MFCreateSample(&sample)));
    QVERIFY(SUCCEEDED(MFCreateMemoryBuffer(DWORD(bytes.size()), &buffer)));
    BYTE *data = nullptr;
    buffer->Lock(&data, nullptr, nullptr);
    memcpy(data, bytes.constData(), size_t(bytes.size()));
    buffer->Unlock();
    buffer->SetCurrentLength(DWORD(bytes.size()));
    sample->AddBuffer(buffer.Get());
    sample->SetSampleDuration(333667);
    cameraMfPacket(sample.Get(), packet, 12345678, false);
    QCOMPARE(packet->pts, int64_t(12345678));
    QCOMPARE(packet->dts, int64_t(AV_NOPTS_VALUE));
    QCOMPARE(packet->duration, int64_t(333667));
    QVERIFY(packet->flags & AV_PKT_FLAG_KEY); // 缺少 CleanPoint 时识别 Annex B IDR。
    QCOMPARE(QByteArray(reinterpret_cast<const char *>(packet->data), packet->size), bytes);
    for (int i = 0; i < AV_INPUT_BUFFER_PADDING_SIZE; ++i)
        QCOMPARE(packet->data[packet->size + i], uint8_t(0));
    av_packet_unref(packet);
    sample->SetUINT64(MFSampleExtension_DecodeTimestamp, 12000000);
    cameraMfPacket(sample.Get(), packet, 12345678, false);
    QCOMPARE(packet->dts, int64_t(12000000));
#else
    QSKIP("Media Foundation requires Windows");
#endif
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
    QVERIFY2(!devices.isEmpty(), "No Media Foundation cameras available");
    VideoSourceManager model;
    CameraSourceController controller(model);
    QStringList ids;
    QHash<QString, QList<CameraMode>> catalogs;
    for (const auto &item : devices) {
        const auto &device = item;
        const QString deviceId = device.id;
        const auto modes = cameraModes(deviceId, &error);
        qInfo().noquote() << "Media Foundation device:" << device.name << "modes:" << modes.size();
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
