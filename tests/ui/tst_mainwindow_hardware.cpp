#include "../video/videosourcetesthelpers.h"
#include "ui/mainwindow.h"
#include "ui/videosourcepanel.h"
#include "ui/sourcedialogs.h"
#include "ui/audiosourcepanel.h"
#include "ui/outputwidgets.h"
#include "ui/previewwidget.h"
#include "video/videosourcemanager.h"
#include "video/videosourceaggregator.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"
#include "audio/audiosourcemanager.h"
#include "output/outputsettings.h"
#include "output/recordingcontroller.h"
#include "output/streamingcontroller.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QProxyStyle>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyleFactory>
#include <QSurfaceFormat>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QToolButton>
#include <QtTest>
#include <cmath>

#include "support/recordingacceptance.h"
#include "support/previewtesthelpers.h"
#include "support/mainwindowtestapp.h"

class MainWindowHardwareTest : public QObject
{
    Q_OBJECT
private slots:
    void recordingAcceptance();
    void cameraHardwarePreview();
    void cameraHardwareOutputs();
};

void MainWindowHardwareTest::recordingAcceptance()
{
    if (qEnvironmentVariableIntValue("OPENLIVE_RECORDING_SOAK_SECONDS") <= 0)
        QSKIP("Explicit real-time recording acceptance only");
    MainWindow window;
    RecordingAcceptance::run(window);
}

void MainWindowHardwareTest::cameraHardwarePreview()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_CAMERA_HARDWARE"))
        QSKIP("Requires explicit camera hardware validation");
    MainWindow window;
    window.show();

    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    QVERIFY2(preview, "Requires an interactive desktop OpenGL preview");
    QTRY_VERIFY_WITH_TIMEOUT(preview->isValid(), 10000);

    auto *model = window.findChild<VideoSourceManager *>();
    auto *controller = window.findChild<CameraSourceController *>();
    controller->refreshDevices();
    QTRY_VERIFY_WITH_TIMEOUT(!controller->refreshing(), 10000);
    QVERIFY2(controller->catalogError().isEmpty(), qPrintable(controller->catalogError()));

    const auto devices = controller->devices();
    QVERIFY(!devices.isEmpty());
    QSignalSpy failures(preview, &PreviewWidget::sourceFailed);
    window.findChild<QToolButton *>("addSourceButton")->click();
    auto *dialog = window.findChild<QDialog *>("sourceAddDialog");
    QVERIFY(dialog);
    dialog->findChild<QComboBox *>("sourceTypeInput")->setCurrentIndex(1);
    auto *confirm = dialog->findChild<QPushButton *>("confirmSourceAddButton");
    QTRY_VERIFY_WITH_TIMEOUT(confirm->isEnabled(), 10000);
    confirm->click();
    QCOMPARE(dialog->result(), int(QDialog::Accepted));
    QCOMPARE(model->sources().size(), 1);
    const QString id = model->sources().first()->id();
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model->sourceState(id), VideoSourceState::Capturing, 15000);

    const auto filter = model->addFilter(id);
    QVERIFY(setFilterConfig(*model, id, filter, ColorCorrectionConfig{0, 1, 0}));
    QTest::qWait(500);
    QVERIFY(preview->lastError().isEmpty());
    QVERIFY(failures.isEmpty());

    const auto frame = grabPreview(*preview);
    QVERIFY(!frame.isNull());
    const QColor pixel = frame.pixelColor(frame.width() / 2, frame.height() / 2);
    QVERIFY(qAbs(pixel.red() - pixel.green()) <= 3 && qAbs(pixel.green() - pixel.blue()) <= 3);
    // 保持鼠标按下，让真实摄像头帧穿插到编辑过程中，验证管理器、控制器与预览之间的完整往返链路。
    const VideoSourceTransform start{{960, 540}, {600, 300}, 0};
    for (int operation = 0; operation < 3; ++operation) {
        QVERIFY(model->setSourceTransform(id, start));
        QTest::qWait(100);

        const auto display = canvasToWidget(preview->size());
        const auto center = display.map(start.center());
        const QPointF from =
            operation == 0 ? center
            : operation == 1
                ? display.map(start.sourceToCanvas().map(QPointF(600, 300)))
                : display.map(start.sourceToCanvas().map(QPointF(300, 0))) - QPointF(0, 24);
        QPointF to;
        if (operation == 2) {
            QTransform rotation;
            rotation.rotate(30);
            to = center + rotation.map(from - center);
        } else {
            const QPointF delta = operation == 0 ? QPointF(120, 60) : QPointF(60, 30);
            to = from + display.map(delta) - display.map(QPointF());
        }

        const auto mouse = [&](QEvent::Type type, const QPointF &position, Qt::MouseButton button,
                               Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, preview->mapToGlobal(position), button, buttons,
                              Qt::NoModifier);
            QApplication::sendEvent(preview, &event);
        };
        mouse(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        QTest::qWait(150);
        mouse(QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton);
        QTest::qWait(150);
        mouse(QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton);

        const auto actual = model->sources().at(0)->transform();
        if (operation == 0)
            QVERIFY(QLineF(actual.center(), QPointF(1080, 600)).length() < 0.001);
        else if (operation == 1) {
            QVERIFY(qAbs(actual.size().width() - 660) < 0.001);
            QVERIFY(qAbs(actual.size().height() - 330) < 0.001);
        } else
            QVERIFY(qAbs(actual.rotationDegrees() - 30) < 0.001);
    }
    window.resize(1100, 720);
    QTest::qWait(100);
    QVERIFY(preview->lastError().isEmpty());
    window.showMinimized();
    QCoreApplication::processEvents();

    const auto minimizedSequence = preview->compositor()->latestFrame()->sequence;
    const auto minimizedRevision = preview->compositor()->latestFrame()->revision;
    QTRY_VERIFY(preview->compositor()->latestFrame()->sequence > minimizedSequence);
    QTRY_VERIFY(preview->compositor()->latestFrame()->revision > minimizedRevision);
    QVERIFY(preview->compositor()->latestFrame()->sources.contains(id));
    QCOMPARE(controller->activeWorkerCount(), 1);
    QVERIFY(!preview->compositor()->captureImage().isNull());
    window.showNormal();
    QTRY_VERIFY(window.isVisible());
    QTRY_VERIFY(preview->isValid());
    QVERIFY(model->setSourceVisible(id, false));
    QTRY_COMPARE_WITH_TIMEOUT(controller->activeWorkerCount(), 0, 10000);
    QVERIFY(model->removeSource(id));
}

void MainWindowHardwareTest::cameraHardwareOutputs()
{
    if (!qEnvironmentVariableIsSet("OPENLIVE_CAMERA_HARDWARE"))
        QSKIP("Requires explicit camera hardware validation");
    MainWindow window;
    window.show();
    auto *model = window.findChild<VideoSourceManager *>();
    auto *camera = window.findChild<CameraSourceController *>();
    auto *recording = window.findChild<RecordingController *>();
    auto *streaming = window.findChild<StreamingController *>();
    QVERIFY(recording && streaming);
    const auto devices = cameraDevices();
    QVERIFY(!devices.isEmpty());
    const auto id = addCamera(*camera, "USB camera", cameraRequest(devices[0].id));
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model->sourceState(id), VideoSourceState::Capturing, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(recording->ready() && streaming->ready(), 10000);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer port;
    QVERIFY(port.listen(QHostAddress::LocalHost));
    const auto server = QStringLiteral("rtmp://127.0.0.1:%1/live").arg(port.serverPort());
    port.close();
    QProcess receiver;
    const auto stopReceiver = qScopeGuard([&] {
        if (receiver.state() != QProcess::NotRunning) {
            receiver.kill();
            receiver.waitForFinished(3000);
        }
    });
    receiver.start(
        QDir(QFileInfo(QStringLiteral(OPENLIVE_FFPROBE)).absolutePath()).filePath("ffmpeg"),
        {"-hide_banner", "-loglevel", "error", "-listen", "1", "-i", server + "/camera", "-c",
         "copy", "-f", "flv", directory.filePath("received.flv")});
    QVERIFY(receiver.waitForStarted(5000));
    QVERIFY(recording->startRecording(directory.filePath("camera.mp4")));
    QVERIFY(streaming->startStreaming(server, "camera"));
    QTRY_VERIFY_WITH_TIMEOUT(streaming->state() == StreamingState::Live ||
                                 receiver.state() == QProcess::NotRunning,
                             15000);
    QVERIFY2(
        streaming->state() == StreamingState::Live,
        qPrintable(streaming->lastError() + QString::fromUtf8(receiver.readAllStandardError())));
    const auto generation = camera->m_entries.at(id).generation;
    camera->reportCaptureFailure(id, generation, "Injected camera outage during output");
    QCOMPARE(recording->state(), RecordingState::Recording);
    QCOMPARE(streaming->state(), StreamingState::Live);
    QTRY_COMPARE_WITH_TIMEOUT(model->sourceState(id), VideoSourceState::Capturing, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(recording->durationMs() >= 1500, 10000);
    recording->stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!recording->busy(), 15000);
    QVERIFY2(recording->fileComplete(), qPrintable(recording->lastError()));
    QCOMPARE(streaming->state(), StreamingState::Live);
    QVERIFY(recording->startRecording(directory.filePath("camera-second.mp4")));
    QTRY_COMPARE_WITH_TIMEOUT(recording->state(), RecordingState::Recording, 10000);
    streaming->stopStreaming();
    QTRY_VERIFY_WITH_TIMEOUT(!streaming->busy(), 15000);
    QCOMPARE(recording->state(), RecordingState::Recording);
    recording->stopRecording();
    QTRY_VERIFY_WITH_TIMEOUT(!recording->busy(), 15000);
    QVERIFY(recording->fileComplete());
    QTRY_VERIFY_WITH_TIMEOUT(receiver.state() == QProcess::NotRunning, 10000);
    QVERIFY(QFileInfo(directory.filePath("received.flv")).size() > 1000);
    QProcess probe;
    probe.start(QStringLiteral(OPENLIVE_FFPROBE),
                {"-v", "error", "-select_streams", "v:0", "-show_entries",
                 "stream=codec_name,width,height,avg_frame_rate", "-of", "json",
                 directory.filePath("camera.mp4")});
    QVERIFY(probe.waitForFinished(10000));
    QCOMPARE(probe.exitCode(), 0);
    const auto video = QJsonDocument::fromJson(probe.readAllStandardOutput())
                           .object()
                           .value("streams")
                           .toArray()
                           .at(0)
                           .toObject();
    QCOMPARE(video.value("codec_name").toString(), QString("h264"));
    QCOMPARE(video.value("width").toInt(), 1920);
    QCOMPARE(video.value("height").toInt(), 1080);
    QCOMPARE(video.value("avg_frame_rate").toString(), QString("30/1"));
}

int main(int argc, char **argv)
{
    return runMainWindowTests<MainWindowHardwareTest>(argc, argv);
}

#include "tst_mainwindow_hardware.moc"
