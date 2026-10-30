#include "capture/captureframe.h"
#include "capture/d3dtexture.h"
#include "capture/gpuinterop.h"
#include "capture/displaycaptureworker.h"
#include "capture/displaytargets.h"
#include "capture/displaycapturecontroller.h"
#include "core/sourcelistmodel.h"
#include "ui/previewwidget.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLExtraFunctions>
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QPainter>
#include <thread>
#ifdef Q_OS_WIN
#include <psapi.h>
#endif

class AnimatedCaptureTarget : public QWidget {
public:
    AnimatedCaptureTarget()
    {
        resize(192, 128);
        move(30, 30);
        m_timer.setInterval(16);
        connect(&m_timer, &QTimer::timeout, this, [this] { ++m_frame; update(); });
        m_timer.start();
    }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::red);
        painter.fillRect(m_frame % width(), 0, 32, height(), Qt::green);
    }
private:
    QTimer m_timer;
    int m_frame = 0;
};

class CaptureInteropTest : public QObject {
    Q_OBJECT
private:
    static QImage grabPreview(PreviewWidget &preview)
    {
        preview.compositor()->captureImage();
        QCoreApplication::processEvents();
        return preview.grabFramebuffer();
    }
private slots:
    void removalDuringErrorNotificationIsSafe()
    {
        SourceListModel model;
        DisplayCaptureController controller(model);
        connect(&controller, &DisplayCaptureController::sourceError, &model,
            [&model](const QString &id, const QString &) { model.removeSource(id); });
        QVERIFY(!controller.addDisplay("Missing", "missing-display", true).isEmpty());
        QTRY_COMPARE(model.rowCount(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
    }

    void transferFailureStopsAndRequiresExplicitRetry()
    {
        SourceListModel model;
        DisplayCaptureController controller(model);
        QSignalSpy errors(&controller, &DisplayCaptureController::sourceError);
        const QString id = controller.addDisplay("Screen", "missing-display", true);
        controller.failTransfer(id, 0, "stale generation");
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("starting"));
        controller.failTransfer(id, 1, "unsupported interop");
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("error"));
        QVERIFY(model.displayProperties(id).value("error").toString().contains("GPU"));
        QCOMPARE(model.rowCount(), 1);
        controller.failTransfer(id, 1, "repeated failure");
        QCOMPARE(errors.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
        QTest::qWait(100);
        QCOMPARE(errors.count(), 1);
        QVERIFY(controller.retry(id));
        controller.failTransfer(id, 1, "old failure after retry");
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("starting"));
        controller.failTransfer(id, 2, "current failure");
        QCOMPARE(errors.count(), 2);
        QVERIFY(model.removeSource(id));
        controller.failTransfer(id, 2, "failure after removal");
        QCOMPARE(errors.count(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
    }

    void failedSourcesStayEditableAndDoNotRetry()
    {
        SourceListModel model;
        DisplayCaptureController controller(model);
        QList<PreviewSource> snapshot;
        connect(&controller, &DisplayCaptureController::sourcesChanged, this,
            [&](const auto &sources) { snapshot = sources; });
        QSignalSpy errors(&controller, &DisplayCaptureController::sourceError);
        const auto id = controller.addDisplay("Missing", "missing-display", true);
        QVERIFY(!id.isEmpty());
        QCOMPARE(model.rowCount(), 1);
        QTRY_COMPARE(model.displayProperties(id).value("state").toString(), QString("error"));
        QCOMPARE(errors.count(), 1);
        QCOMPARE(snapshot.size(), 1);
        QVERIFY(!snapshot[0].hasFrame());
        QTRY_COMPARE(controller.activeWorkerCount(), 0);
        QTest::qWait(200);
        QCOMPARE(errors.count(), 1);
        const SourceTransform transform{{400, 300}, {500, 250}, 25};
        QVERIFY(model.setSourceTransform(id, transform));
        const QString filter = model.addColorCorrection(id);
        QVERIFY(!filter.isEmpty());
        QVERIFY(controller.applyProperties(id, "another-missing-display", false));
        QCOMPARE(model.index(0, 0).data(SourceListModel::TransformRole).value<SourceTransform>(), transform);
        QCOMPARE(model.sourceFilters(id)[0].toMap()["id"].toString(), filter);
        QVERIFY(!model.displayProperties(id).value("captureCursor").toBool());
        QTRY_COMPARE(errors.count(), 2);
        QVERIFY(model.setSourceVisible(id, false));
        QVERIFY(controller.retry(id));
        QTest::qWait(100);
        QCOMPARE(controller.activeWorkerCount(), 0);
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("paused"));
        QVERIFY(model.removeSource(id));
        QVERIFY(snapshot.isEmpty());
        QVERIFY(!controller.retry(id));
    }

    void controllerLifecycleAndGpuPreview()
    {
        const auto targets = displayTargets();
        if (targets.isEmpty()) QSKIP("Interactive Windows desktop required");
        qInfo() << "Capture target resolution:" << targets[0].size;
        AnimatedCaptureTarget animation;
        animation.show();
        SourceListModel model;
        DisplayCaptureController controller(model);
        PreviewWidget preview;
        preview.resize(480, 270);
        preview.move(500, 400);
        preview.show();
        if (!QTest::qWaitForWindowExposed(&preview) || !preview.isValid()) {
            QVERIFY2(!qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP"), "Interactive OpenGL desktop required");
            QSKIP("Interactive OpenGL desktop unavailable");
        }
        QList<PreviewSource> snapshot;
        connect(&controller, &DisplayCaptureController::sourcesChanged, &preview, &PreviewWidget::setSources);
        connect(&controller, &DisplayCaptureController::sourcesChanged, this,
            [&](const auto &sources) { snapshot = sources; });
        connect(&preview, &PreviewWidget::gpuTransferFailed, &controller, &DisplayCaptureController::failTransfer, Qt::QueuedConnection);
        QSignalSpy errors(&controller, &DisplayCaptureController::sourceError);
        QSignalSpy transferFailures(&preview, &PreviewWidget::gpuTransferFailed);
        const QString id = controller.addDisplay("Screen", targets[0].id, true);
        const auto hasFrame = [&] { return snapshot.size() == 1 && snapshot[0].hasFrame(); };
        QTRY_VERIFY_WITH_TIMEOUT(hasFrame() || !model.displayProperties(id).value("error").toString().isEmpty(), 5000);
        if (!hasFrame()) {
            QVERIFY2(!qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP"), qPrintable(model.displayProperties(id).value("error").toString()));
            QSKIP("Windows capture unavailable on this desktop");
        }
        QVERIFY(!grabPreview(preview).isNull());
        QCoreApplication::processEvents();
        if (!transferFailures.isEmpty()) {
            QVERIFY2(!qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP"), "GPU interop required");
            QSKIP("GPU interop unavailable on this desktop");
        }
        const QString filter = model.addColorCorrection(id);
        QVERIFY(model.setFilterParameters(id, filter, .1, .8, 1.2));
        QVERIFY(model.setSourceTransform(id, {{960, 540}, {1200, 700}, 17}));
        QVERIFY(!grabPreview(preview).isNull());

        const int seconds = qEnvironmentVariableIntValue("OPENLIVE_CAPTURE_SOAK_SECONDS");
        if (seconds > 0) {
            QElapsedTimer soak; soak.start();
            quint64 previous = 0;
            qint64 updates = 0, maximumUiGap = 0, lastTick = soak.elapsed();
#ifdef Q_OS_WIN
            SIZE_T baselineBytes = 0, lastBytes = 0;
            DWORD baselineHandles = 0, lastHandles = 0;
#endif
            while (soak.elapsed() < seconds * 1000LL) {
                QTest::qWait(20);
                const qint64 now = soak.elapsed();
                maximumUiGap = qMax(maximumUiGap, now - lastTick);
                lastTick = now;
                QVERIFY(model.displayProperties(id).value("error").toString().isEmpty());
                QCOMPARE(controller.activeWorkerCount(), 1);
                if (hasFrame() && snapshot[0].frame->sequence != previous) {
                    previous = snapshot[0].frame->sequence;
                    ++updates;
                }
                if (updates % 90 == 0) {
                    model.setFilterParameters(id, filter, .05, .9, 1.1);
                    preview.resize(updates % 180 == 0 ? QSize(480, 270) : QSize(500, 300));
                }
#ifdef Q_OS_WIN
                if (soak.elapsed() > 10000 && (!baselineBytes || updates % 300 == 0)) {
                    PROCESS_MEMORY_COUNTERS_EX memory{};
                    QVERIFY(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory)));
                    lastBytes = memory.PrivateUsage;
                    QVERIFY(GetProcessHandleCount(GetCurrentProcess(), &lastHandles));
                    if (!baselineBytes) { baselineBytes = lastBytes; baselineHandles = lastHandles; }
                }
#endif
            }
            qInfo() << "GPU soak seconds / observed frames / maximum GUI tick gap (ms):"
                    << seconds << updates << maximumUiGap;
            QVERIFY(updates > seconds * 10);
            QVERIFY(maximumUiGap < 250);
            QCOMPARE(transferFailures.count(), 0);
#ifdef Q_OS_WIN
            if (baselineBytes) {
                qInfo() << "Private bytes baseline/end:" << baselineBytes << lastBytes
                        << "process handles baseline/end:" << baselineHandles << lastHandles;
                QVERIFY(lastBytes <= baselineBytes + 64 * 1024 * 1024);
                QVERIFY(lastHandles <= baselineHandles + 32);
            }
#endif
        }

        const auto duplicate = controller.addDisplay("Same screen", targets[0].id, false);
        QTRY_VERIFY_WITH_TIMEOUT(snapshot.size() == 2 && snapshot[0].hasFrame() && snapshot[1].hasFrame(), 5000);
        QVERIFY(snapshot[0].frame->generation != snapshot[1].frame->generation);
        QVERIFY(snapshot[0].filters.isEmpty());
        QVERIFY(!grabPreview(preview).isNull());
        const bool duplicateGpu = bool(snapshot[0].frame->gpu);
        std::weak_ptr<GpuFrameResource> releasedGpu = snapshot[0].frame->gpu;
        preview.showMinimized();
        const quint64 composedBeforeMinimize = preview.compositor()->latestFrame()->sequence;
        const quint64 beforeMinimize = snapshot[1].frame->sequence;
        QTRY_VERIFY(snapshot[1].frame->sequence > beforeMinimize);
        QTRY_VERIFY(preview.compositor()->latestFrame()->sequence > composedBeforeMinimize);
        QVERIFY(model.setSourceVisible(duplicate, false));
        QVERIFY(!snapshot[0].hasFrame() && snapshot[1].hasFrame());
        QVERIFY(model.removeSource(duplicate));
        QTRY_VERIFY(!preview.compositor()->latestFrame()->sources.contains(duplicate));
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 1, 2000);
        if (duplicateGpu) QTRY_VERIFY_WITH_TIMEOUT(releasedGpu.expired(), 2000);
        preview.showNormal();
        QVERIFY(QTest::qWaitForWindowExposed(&preview));
        QTRY_VERIFY(hasFrame());
        const quint64 oldGeneration = snapshot[0].frame->generation;
        QVERIFY(model.setSourceVisible(id, false));
        QVERIFY(!snapshot[0].hasFrame());
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
        QVERIFY(model.setSourceVisible(id, true));
        QVERIFY(!snapshot[0].hasFrame());
        QTRY_VERIFY_WITH_TIMEOUT(hasFrame(), 5000);
        QVERIFY(snapshot[0].frame->generation > oldGeneration);
        const auto transform = snapshot[0].transform;
        QVERIFY(controller.applyProperties(id, targets[0].id, false));
        QTRY_VERIFY_WITH_TIMEOUT(hasFrame(), 5000);
        QCOMPARE(snapshot[0].transform, transform);
        QCOMPARE(snapshot[0].filters[0].id, filter);

        qputenv("OPENLIVE_FORCE_INTEROP_FAILURE", "1");
        const quint64 beforeFailureGeneration = snapshot[0].frame->generation;
        QVERIFY(controller.retry(id));
        QTRY_COMPARE_WITH_TIMEOUT(model.displayProperties(id).value("state").toString(), QString("error"), 5000);
        qunsetenv("OPENLIVE_FORCE_INTEROP_FAILURE");
        QCOMPARE(transferFailures.count(), 1);
        QCOMPARE(errors.count(), 1);
        QVERIFY(!snapshot[0].hasFrame());
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
        QTest::qWait(150);
        QVERIFY(!snapshot[0].hasFrame());
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("error"));
        QVERIFY(controller.retry(id));
        QTRY_VERIFY_WITH_TIMEOUT(hasFrame(), 5000);
        QVERIFY(snapshot[0].frame->gpu && snapshot[0].frame->generation > beforeFailureGeneration);
        QVERIFY(!grabPreview(preview).isNull());
        QCoreApplication::processEvents();
        QCOMPARE(transferFailures.count(), 1);
        controller.failTransfer(id, beforeFailureGeneration, "late failure");
        QCOMPARE(model.displayProperties(id).value("state").toString(), QString("capturing"));
        QVERIFY(model.removeSource(id));
        QVERIFY(snapshot.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
        for (int i = 0; i < 8; ++i) {
            const auto repeated = controller.addDisplay("Repeat", targets[0].id, i % 2);
            QTRY_VERIFY_WITH_TIMEOUT(hasFrame(), 5000);
            QVERIFY(model.removeSource(repeated));
            QTRY_COMPARE_WITH_TIMEOUT(controller.activeWorkerCount(), 0, 2000);
        }
    }

    void latestSlotNeverQueuesFrames()
    {
        LatestCaptureFrame slot;
        std::weak_ptr<const CaptureFrame> previous;
        for (quint64 i = 1; i <= 10000; ++i) {
            auto frame = std::make_shared<CaptureFrame>();
            frame->sequence = i;
            slot.publish(frame);
            QVERIFY(previous.expired());
            previous = frame;
        }
        auto latest = slot.take();
        QCOMPARE(latest->sequence, quint64(10000));
        QVERIFY(!slot.take());
        latest.reset();
        QVERIFY(previous.expired());
        std::thread producer([&] {
            for (quint64 i = 1; i <= 10000; ++i) {
                auto frame = std::make_shared<CaptureFrame>();
                frame->sequence = i;
                slot.publish(frame);
            }
        });
        quint64 last = 0;
        for (int i = 0; i < 10000; ++i) {
            if (auto frame = slot.take()) { QVERIFY(frame->sequence > last); last = frame->sequence; }
        }
        producer.join();
        if (auto frame = slot.take()) { QVERIFY(frame->sequence > last); last = frame->sequence; }
        QCOMPARE(last, quint64(10000));
    }

    void sharedTextureCopiesAndReleases()
    {
#ifdef Q_OS_WIN
        QOpenGLContext glContext;
        if (!glContext.create()) {
            QVERIFY(!qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP"));
            QSKIP("OpenGL context unavailable");
        }
        QOffscreenSurface surface;
        surface.setFormat(glContext.format());
        surface.create();
        QVERIFY(glContext.makeCurrent(&surface));
        auto *gl = glContext.extraFunctions();
        auto resource = std::make_shared<GpuFrameResource>();
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext;
        QVERIFY(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            &resource->device, nullptr, &d3dContext)));
        qint64 maximum = 0;
        for (const QSize size : {QSize(64, 32), QSize(1920, 1080), QSize(320, 240)}) {
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = UINT(size.width()); desc.Height = UINT(size.height());
            desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            QVector<QRgb> pixels(size.width() * size.height(), qRgba(230, 20, 40, 128));
            for (int y = size.height() / 2; y < size.height(); ++y)
                std::fill(pixels.begin() + y * size.width(), pixels.begin() + (y + 1) * size.width(), qRgba(10, 30, 220, 255));
            D3D11_SUBRESOURCE_DATA initial{pixels.constData(), UINT(size.width() * 4), 0};
            resource->texture.Reset();
            QVERIFY(SUCCEEDED(resource->device->CreateTexture2D(&desc, &initial, &resource->texture)));
            CaptureFrame frame;
            frame.size = size;
            frame.gpu = resource;
            QOpenGLFramebufferObject destination(size);
            QVERIFY(destination.isValid());
            {
                GpuInterop bridge;
                for (int i = 0; i < 60; ++i) {
                    QString error;
                    QElapsedTimer elapsed; elapsed.start();
                    const bool copied = bridge.copy(frame, destination.texture(), &error);
                    if (!copied && !qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP")) QSKIP(qPrintable(error));
                    QVERIFY2(copied, qPrintable(error));
                    maximum = qMax(maximum, elapsed.nsecsElapsed());
                    QVERIFY(destination.bind());
                    uchar pixel[4]{};
                    gl->glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                    QCOMPARE(int(pixel[0]), 230); QCOMPARE(int(pixel[2]), 40); QCOMPARE(int(pixel[3]), 128);
                    gl->glReadPixels(4, size.height() - 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                    QCOMPARE(int(pixel[0]), 10); QCOMPARE(int(pixel[2]), 220);
                }
                qputenv("OPENLIVE_FORCE_INTEROP_FAILURE", "1");
                QString error;
                QVERIFY(!bridge.copy(frame, destination.texture(), &error));
                qunsetenv("OPENLIVE_FORCE_INTEROP_FAILURE");
            }
            if (size == QSize(64, 32)) {
                QWidget replacementHost;
                PreviewWidget preview;
                preview.resize(480, 270);
                preview.show();
                QVERIFY(QTest::qWaitForWindowExposed(&preview));
                auto gpuFrame = std::make_shared<CaptureFrame>();
                gpuFrame->size = size; gpuFrame->generation = 1; gpuFrame->sequence = 1;
                gpuFrame->gpu = resource;
                PreviewSource source;
                source.id = "synthetic";
                source.transform = {{960, 540}, {1920, 1080}, 0};
                source.frame = gpuFrame;
                SourceFilter correction{"correction"}; correction.brightness = .1f;
                source.filters = {correction};
                preview.setSources({source});
                const auto gpuResult = grabPreview(preview);
                const QColor top = gpuResult.pixelColor(gpuResult.width() / 2, gpuResult.height() / 4);
                QVERIFY(qAbs(top.red() - 128) <= 1);
                QVERIFY(qAbs(top.green() - 23) <= 1);
                source.frame.reset();
                source.image = QImage(reinterpret_cast<const uchar *>(pixels.constData()), size.width(), size.height(), QImage::Format_ARGB32).copy();
                preview.setSources({source});
                const auto imageResult = grabPreview(preview);
                QCOMPARE(imageResult.pixelColor(imageResult.width() / 2, imageResult.height() / 4), top);
                QCOMPARE(imageResult.pixelColor(imageResult.width() / 2, imageResult.height() * 3 / 4),
                         gpuResult.pixelColor(gpuResult.width() / 2, gpuResult.height() * 3 / 4));
                source.frame = gpuFrame;
                source.image = {};
                preview.setSources({source});
                grabPreview(preview);
                replacementHost.resize(480, 270);
                preview.setParent(&replacementHost);
                preview.resize(480, 270);
                replacementHost.show();
                preview.show();
                QVERIFY(QTest::qWaitForWindowExposed(&replacementHost));
                const auto restored = grabPreview(preview);
                QCOMPARE(restored.pixelColor(restored.width() / 2, restored.height() / 4), top);
                preview.setSources({});
                QVERIFY(glContext.makeCurrent(&surface));
            }
            QVERIFY(glContext.makeCurrent(&surface));
            QCOMPARE(gl->glGetError(), GLenum(GL_NO_ERROR));
        }
        qInfo() << "Maximum GPU import/copy time (ms):" << maximum / 1000000.0;
        QVERIFY(maximum < 100000000); // Reject a route that visibly stalls GUI work.
#else
        QSKIP("Windows D3D11 required");
#endif
    }

    void invalidTargetStopsWithoutRetry()
    {
        DisplayCaptureWorker worker("missing-display", true, 1);
        worker.start();
        QVERIFY(worker.wait(2000));
        QVERIFY(!worker.error().isEmpty());
        QVERIFY(!worker.takeLatest());
    }

    void realCaptureProducesGpuFramesAndStops()
    {
        const auto targets = displayTargets();
        if (targets.isEmpty()) QSKIP("Interactive Windows desktop required");
        DisplayCaptureWorker worker(targets[0].id, true, 9);
        worker.start();
        std::shared_ptr<const CaptureFrame> frame;
        QTRY_VERIFY_WITH_TIMEOUT(frame || (frame = worker.takeLatest()) || !worker.error().isEmpty(), 5000);
        if (!worker.error().isEmpty() && !qEnvironmentVariableIsSet("OPENLIVE_REQUIRE_GPU_INTEROP")) QSKIP(qPrintable(worker.error()));
        QVERIFY2(worker.error().isEmpty(), qPrintable(worker.error()));
        QVERIFY(frame && frame->gpu);
        QCOMPARE(frame->generation, quint64(9));
        QCOMPARE(frame->size, targets[0].size);
        frame.reset();
        const int seconds = qEnvironmentVariableIntValue("OPENLIVE_CAPTURE_SOAK_SECONDS");
        if (seconds > 0) {
            QElapsedTimer soak; soak.start();
            quint64 last = 0;
            while (soak.elapsed() < seconds * 1000LL) {
                if (auto next = worker.takeLatest()) { QVERIFY(next->sequence > last); last = next->sequence; }
                QVERIFY2(worker.error().isEmpty(), qPrintable(worker.error()));
                QTest::qWait(50);
            }
            qInfo() << "Completed capture soak (seconds):" << seconds;
        }
        QElapsedTimer stop; stop.start();
        worker.stop();
        QVERIFY(worker.wait(2000));
        qInfo() << "Capture shutdown (ms):" << stop.elapsed();
    }
};

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc, argv);
    CaptureInteropTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_captureinterop.moc"
