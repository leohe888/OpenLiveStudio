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

#include "support/previewtesthelpers.h"
#include "support/mainwindowtestapp.h"

class MainWindowGpuTest : public QObject
{
    Q_OBJECT
private slots:
    void previewDisplaysImageSources();
    void previewEditsModelAndSynchronizesSelection();
    void previewShowsRenderingError();
    void recordingControlsAndGracefulClose();
    void streamingControlsAndGracefulClose();
};

void MainWindowGpuTest::previewDisplaysImageSources()
{
    MainWindow window;

    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required for preview integration testing.");
    window.resize(1100, 820); // 使用非 16:9 视口，验证画布适配后留下的边缘区域。
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QTRY_VERIFY(preview->isValid());
    QVERIFY2(preview->lastError().isEmpty(), qPrintable(preview->lastError()));

    auto *pages = window.findChild<QStackedWidget *>("previewPages");
    QCOMPARE(pages->currentWidget(), preview);

    auto *sources = window.findChild<VideoSourceManager *>();
    QVERIFY(sources);
    QCOMPARE(sources->sources().size(), 0);

    const auto empty = grabPreview(*preview);
    QCOMPARE(empty.pixelColor(empty.width() / 2, empty.height() / 2), QColor(Qt::black));
    QTemporaryDir images;
    QImage image(1920, 1080, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.fillRect(0, 0, 960, 540, Qt::red);
        painter.fillRect(960, 0, 960, 540, Qt::green);
        painter.fillRect(0, 540, 960, 540, Qt::blue);
        painter.fillRect(960, 540, 960, 540, Qt::white);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(800, 450, 320, 180, Qt::transparent);
    }
    const QString imagePath = images.filePath("corners.png");
    QVERIFY(image.save(imagePath));

    auto *controller = window.findChild<ImageSourceController *>();
    QVERIFY(controller);
    QObject request;
    controller->addImage("Corners", imagePath, &request);
    QTRY_COMPARE(sources->sources().size(), 1);

    const auto verifyFrame = [&] {
        const QImage frame = grabPreview(*preview);
        QVERIFY(!frame.isNull());
        const double scale = qMin(frame.width() / 1920.0, frame.height() / 1080.0);
        const double width = 1920 * scale;
        const double height = 1080 * scale;
        const double x = (frame.width() - width) / 2;
        const double y = (frame.height() - height) / 2;

        const auto sample = [&](double u, double v) {
            return frame.pixelColor(int(x + u * width), int(y + v * height));
        };
        QCOMPARE(sample(.1, .1), QColor(Qt::red));
        QCOMPARE(sample(.9, .1), QColor(Qt::green));
        QCOMPARE(sample(.1, .9), QColor(Qt::blue));
        QCOMPARE(sample(.9, .9), QColor(Qt::white));
        QCOMPARE(sample(.5, .5), QColor(Qt::black));
        QVERIFY(x > 2 || y > 2);
        const QColor surrounding = y > 2 ? frame.pixelColor(frame.width() / 2, int(y / 2))
                                         : frame.pixelColor(int(x / 2), frame.height() / 2);
        QCOMPARE(surrounding, QColor("#D4D7DC"));
    };
    verifyFrame();

    verifyFrame();
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/mainwindow-preview.png"));
    window.resize(qMax(window.minimumWidth(), window.width() - 40), window.height());
    QCoreApplication::processEvents();
    verifyFrame();

    verifyFrame();
    QImage overlay(600, 400, QImage::Format_RGBA8888);
    overlay.fill(Qt::yellow);
    const QString overlayPath = images.filePath("overlay.png");
    QVERIFY(overlay.save(overlayPath));
    controller->addImage("Overlay", overlayPath, &request);
    QTRY_COMPARE(sources->sources().size(), 2);
    const QString overlayId = sources->currentSourceId();

    const auto sampleCanvas = [&](double u, double v) {
        const QImage frame = grabPreview(*preview);
        const double scale = qMin(frame.width() / 1920.0, frame.height() / 1080.0);
        return frame.pixelColor(int((frame.width() - 1920 * scale) / 2 + u * 1920 * scale),
                                int((frame.height() - 1080 * scale) / 2 + v * 1080 * scale));
    };
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));

    auto *list = window.findChild<QListWidget *>("sourceList");
    QCOMPARE(list->count(), 2);

    auto *row = list->itemWidget(list->item(0));
    auto *visibility = row->findChild<QToolButton *>("sourceVisibilityButton");
    auto *lock = row->findChild<QToolButton *>("sourceLockButton");
    auto *down = window.findChild<QToolButton *>("moveSourceDownButton");
    QVERIFY(visibility && lock && down);

    const auto click = [](QToolButton *button) {
        QTest::mouseClick(button, Qt::LeftButton);
    };
    click(lock);
    click(visibility);
    QTRY_VERIFY(!sources->sources().at(0).visible());
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    click(visibility);
    QTRY_VERIFY(sources->sources().at(0).visible());
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    click(down);
    QTRY_COMPARE(sources->sources().at(1).id(), overlayId);
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    QVERIFY(sources->moveSource(overlayId, -1));
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    QVERIFY(sources->removeSource(overlayId));
    verifyFrame();
    QVERIFY(sources->removeSource(sources->sources().at(0).id()));
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::black));
    QVERIFY(window.close());
}

void MainWindowGpuTest::previewEditsModelAndSynchronizesSelection()
{
    MainWindow window;

    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required for mouse editing integration.");
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    auto *model = window.findChild<VideoSourceManager *>();
    auto *controller = window.findChild<ImageSourceController *>();
    QTemporaryDir dir;
    QImage background(1200, 800, QImage::Format_RGBA8888);
    background.fill(Qt::red);
    QImage foreground(400, 200, QImage::Format_RGBA8888);
    foreground.fill(Qt::blue);
    const QString backPath = dir.filePath("back.png"), frontPath = dir.filePath("front.png");
    QVERIFY(background.save(backPath));
    QVERIFY(foreground.save(frontPath));
    QObject request;
    controller->addImage("Background", backPath, &request);
    QTRY_COMPARE(model->sources().size(), 1);
    const QString backId = model->currentSourceId();
    controller->addImage("Foreground", frontPath, &request);
    QTRY_COMPARE(model->sources().size(), 2);
    const QString frontId = model->currentSourceId();

    const auto currentTransform = [&] {
        return model->sources().at(0).transform();
    };

    const auto canvasPoint = [&](const QPointF &point) {
        return canvasToWidget(preview->size()).map(point).toPoint();
    };
    grabPreview(*preview);
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(600, 500)));
    QCOMPARE(model->currentSourceId(), backId);
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, canvasPoint(QPointF(960, 540)));
    QCOMPARE(model->currentSourceId(), frontId);

    const auto move = [&](const QPoint &from, const QPoint &to) {
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(preview, to);
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, to);
    };
    const VideoSourceTransform before = currentTransform();
    const QPoint from = canvasPoint(before.center()), to = from + QPoint(30, 15);
    move(from, to);

    const auto inverse = canvasToWidget(preview->size()).inverted();
    const auto expectedCenter =
        before.center() + inverse.map(QPointF(to)) - inverse.map(QPointF(from));
    QVERIFY(std::hypot(currentTransform().center().x() - expectedCenter.x(),
                       currentTransform().center().y() - expectedCenter.y()) < 1e-6);

    const auto moved = currentTransform();
    window.resize(qMax(window.minimumWidth(), window.width() - 40), window.height());
    QCoreApplication::processEvents();
    QVERIFY(currentTransform() == moved);
    move(canvasPoint(moved.center()), canvasPoint(moved.center()) + QPoint(10, 5));
    QVERIFY(currentTransform().center() != moved.center());
    VideoSourceTransform rotated = currentTransform();
    rotated.setRotationDegrees(30);
    rotated.setSize(QSizeF(450, 250));
    QVERIFY(model->setSourceTransform(frontId, rotated));

    const auto frame = grabPreview(*preview);
    const auto center = canvasPoint(rotated.center());
    QCOMPARE(frame.pixelColor(qRound(center.x() * preview->devicePixelRatioF()),
                              qRound(center.y() * preview->devicePixelRatioF())),
             QColor(Qt::blue));
    QVERIFY(model->setSourceLocked(frontId, true));
    QVERIFY(!model->setSourceTransform(frontId, before));
    QCoreApplication::processEvents(); // 聚合器延后发布变更，处理事件后才能检查最新快照。
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(model->currentSourceId(), backId);
    QVERIFY(model->selectSource(frontId));

    const auto locked = grabPreview(*preview);
    QVERIFY(model->setSourceLocked(frontId, false));

    const auto unlocked = grabPreview(*preview);
    QVERIFY(locked != unlocked);
    QVERIFY(model->setSourceVisible(frontId, false));
    QCoreApplication::processEvents();
    QTest::mouseClick(preview, Qt::LeftButton, Qt::NoModifier, center);
    QCOMPARE(model->currentSourceId(), backId);
    QVERIFY(model->setSourceVisible(frontId, true));
    QVERIFY(model->selectSource(frontId));

    QCoreApplication::processEvents();

    const auto overlayFrame = grabPreview(*preview);
    const auto corner = canvasPoint(rotated.sourceToCanvas().map(QPointF()));
    QCOMPARE(overlayFrame.pixelColor(qRound(corner.x() * preview->devicePixelRatioF()),
                                     qRound(corner.y() * preview->devicePixelRatioF())),
             QColor("#245FBB"));
    QVERIFY(overlayFrame.save(QCoreApplication::applicationDirPath() + "/preview-transforms.png"));

    QCOMPARE(window.findChild<QStackedWidget *>("previewPages")->currentWidget(), preview);
    QVERIFY(preview->lastError().isEmpty());
    QVERIFY(window.close());
}

void MainWindowGpuTest::previewShowsRenderingError()
{
    MainWindow window;

    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required to inject a preview rendering error.");
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *pages = window.findChild<QStackedWidget *>("previewPages");
    auto *error = window.findChild<QLabel *>("previewErrorLabel");
    QVERIFY(pages && error);

    auto *model = window.findChild<VideoSourceManager *>();
    QVERIFY(model->addSource({"example", "Logo"}));
    preview->sourceFailed("example", "Texture upload failed");
    QTRY_VERIFY(window.statusBar()->currentMessage().contains("Logo"));
    QVERIFY(window.statusBar()->currentMessage().contains("Texture upload failed"));
    QCOMPARE(pages->currentWidget(), preview);
    QVERIFY(preview->lastError().isEmpty());
    const QString detail = QStringLiteral("<b>测试渲染错误</b>");
    preview->renderingFailed(detail);
    QCOMPARE(pages->currentWidget(), preview);
    QTRY_COMPARE(pages->currentWidget(), error);
    QVERIFY(error->isVisible());
    QVERIFY(!preview->isVisible());
    QCOMPARE(error->textFormat(), Qt::PlainText);
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + detail);

    QTRY_COMPARE(error->palette().color(QPalette::WindowText), QColor("#B72C3C"));
    QTRY_COMPARE(error->palette().color(QPalette::Window), QColor("#F8FAFC"));
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + detail);
    QCOMPARE(pages->currentWidget(), error);
    QVERIFY(window.close());
}

void MainWindowGpuTest::recordingControlsAndGracefulClose()
{
    MainWindow window;
    window.show();

    auto *recorder = window.findChild<RecordingController *>();
    if (!recorder)
        QSKIP("Native desktop OpenGL required");
    QTRY_VERIFY_WITH_TIMEOUT(recorder->ready(), 5000);

    auto *settings = window.findChild<OutputSettings *>();
    QVERIFY(settings);
    QTemporaryDir directory;
    QVERIFY(settings->apply(directory.path(), 12000, "", ""));

    auto *controls = window.findChild<QWidget *>("controlsPanel");
    QVERIFY(controls);

    auto *record = controls->findChild<QPushButton *>("recordButton");
    QVERIFY(record);
    QTRY_VERIFY(record->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(record, "clicked"));
    QTRY_COMPARE_WITH_TIMEOUT(recorder->state(), RecordingState::Recording, 5000);

    auto *output = window.findChild<QLabel *>("outputStatus");
    QVERIFY(output);
    QTRY_VERIFY_WITH_TIMEOUT(recorder->durationMs() >= 1000, 5000);
    QVERIFY(output->text() != QStringLiteral("录制 00:00:00   |   直播 00:00:00"));
    QVERIFY(output->text().endsWith(QStringLiteral("直播 00:00:00")));
    QVERIFY(settings->recordingLocked());
    QCOMPARE(record->property("text").toString(), QString::fromUtf8("停止录制"));
    controls->findChild<QPushButton *>("settingsButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>();
    QVERIFY(dialog);

    auto *root = dialog.data();
    QVERIFY(!root->findChild<QWidget *>("recordingDirectoryInput")->isEnabled());
    QVERIFY(!root->findChild<QWidget *>("recordingBitrateInput")->isEnabled());
    QVERIFY(!settings->apply(directory.filePath("other"), 16000, "", ""));
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QWidget *>("cancelOutputSettingsButton"),
                                      "clicked"));
    QTRY_VERIFY(dialog.isNull());
    window.showMinimized();
    QTest::qWait(350);
    QCOMPARE(recorder->state(), RecordingState::Recording);
    QVERIFY(!window.close());
    QTRY_VERIFY_WITH_TIMEOUT(!recorder->busy(), 10000);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY2(recorder->lastError().isEmpty(), qPrintable(recorder->lastError()));
    QVERIFY(recorder->fileComplete());
    QVERIFY(QFileInfo(recorder->filePath()).size() > 1000);
    QCOMPARE(output->text(), QStringLiteral("录制 00:00:00   |   直播 00:00:00"));
    QVERIFY(!settings->recordingLocked());
}

void MainWindowGpuTest::streamingControlsAndGracefulClose()
{
    if (QGuiApplication::platformName() == "offscreen")
        QSKIP("Native desktop OpenGL required");
    MainWindow window;
    window.show();

    auto *stream = window.findChild<StreamingController *>();
    QVERIFY(stream);

    auto *recording = window.findChild<RecordingController *>();
    QVERIFY(recording);

    auto *settings = window.findChild<OutputSettings *>();
    QVERIFY(settings);
    QTemporaryDir directory;
    QVERIFY(settings->apply(directory.path(), 12000, "rtmp://127.0.0.1:1/live", "test"));

    auto *controls = window.findChild<QWidget *>("controlsPanel");
    QVERIFY(controls);

    auto *button = controls->findChild<QPushButton *>("streamButton");
    QVERIFY(button);
    QTRY_VERIFY(button->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
    QTRY_COMPARE_WITH_TIMEOUT(stream->state(), StreamingState::Reconnecting, 5000);
    QVERIFY(settings->streamingLocked());
    QVERIFY(!settings->recordingLocked());
    QVERIFY(button->isEnabled());
    controls->findChild<QPushButton *>("settingsButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>();
    QVERIFY(dialog);

    auto *root = dialog.data();
    for (const auto *name : {"streamServerInput", "streamKeyInput", "streamingBitrateInput"})
        QVERIFY(!root->findChild<QWidget *>(name)->isEnabled());
    QVERIFY(root->findChild<QWidget *>("recordingBitrateInput")->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(root->findChild<QWidget *>("cancelOutputSettingsButton"),
                                      "clicked"));
    QTRY_VERIFY(dialog.isNull());
    controls->findChild<QPushButton *>("recordButton")->click();
    QTRY_COMPARE_WITH_TIMEOUT(recording->state(), RecordingState::Recording, 5000);
    QTest::qWait(300);
    QVERIFY(!window.close());
    QTRY_VERIFY_WITH_TIMEOUT(!stream->busy() && !recording->busy(), 5000);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY(recording->fileComplete());

    auto *output = window.findChild<QLabel *>("outputStatus");
    QVERIFY(output);
    QCOMPARE(output->text(), QStringLiteral("录制 00:00:00   |   直播 00:00:00"));
    QVERIFY(!settings->streamingLocked());
    QVERIFY(!settings->recordingLocked());
}

int main(int argc, char **argv)
{
    return runMainWindowTests<MainWindowGpuTest>(argc, argv);
}

#include "tst_mainwindow_gpu.moc"
