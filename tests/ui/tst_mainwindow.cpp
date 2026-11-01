#include "ui/mainwindow.h"
#include "ui/sourcewidgets.h"
#include "ui/audiowidgets.h"
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
#include <QToolButton>
#include <QtTest>
#include <cmath>
#include "support/recordingacceptance.h"

namespace {
QImage grabPreview(PreviewWidget &preview)
{
    QCoreApplication::processEvents();
    preview.compositor()->captureImage();
    QCoreApplication::processEvents();
    return preview.grabFramebuffer();
}
}

class MainWindowTest : public QObject
{
    Q_OBJECT
private slots:
    void shellAndPalette();
    void styleSheetControlsCustomPainting();
    void sourceActionsAndSelection();
    void imageDialogAndCancellation();
    void removalAndFilters();
    void disconnectedDeviceDrafts();
    void audioControlsAndDialogs();
    void outputDraftsAndLocks();
    void scrollingAndKeyboard();
    void aggregatesIndependentSourceControllers();
    void previewDisplaysImageSources();
    void previewEditsModelAndSynchronizesSelection();
    void previewShowsRenderingError();
    void dockTitlesStayVerticallyCentered();
    void keepsOutputStatusVisibleWhenResized();
    void recordingAcceptance();
    void cameraHardwarePreview();
    void recordingControlsAndGracefulClose();
    void streamingControlsAndGracefulClose();
};

void MainWindowTest::shellAndPalette()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();
    QCOMPARE(window.findChildren<QDockWidget *>().size(), 3);
    for (const auto *id : {"sourcesDock", "mixerDock", "controlsDock"}) {
        auto *dock = window.findChild<QDockWidget *>(id);
        QVERIFY(dock);
        QVERIFY(dock->widget());
        QCOMPARE(dock->features(), QDockWidget::NoDockWidgetFeatures);
        QVERIFY(!dock->isFloating());
    }
    QVERIFY(window.findChild<SourcesPanel *>());
    QVERIFY(window.findChild<MixerPanel *>());
    QCOMPARE(window.palette().color(QPalette::Window), QColor("#E9EDF2"));
    QCOMPARE(window.palette().color(QPalette::Text), QColor("#202936"));
    QVERIFY(window.findChild<QLabel *>("outputStatus")->isVisible());
    for (auto *button : window.findChildren<QToolButton *>()) {
        if (!button->objectName().isEmpty()) {
            QVERIFY(!button->icon().isNull());
            QVERIFY(!button->accessibleName().isEmpty());
        }
    }
    QVERIFY(window.grab().save(QCoreApplication::applicationDirPath() + "/widgets-empty.png"));
    QVERIFY(window.close());
}

void MainWindowTest::styleSheetControlsCustomPainting()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *meter = window.findChild<QWidget *>("levelTrack");
    QVERIFY(meter);
    QCOMPARE(meter->property("normalColor").value<QColor>(), QColor("#197349"));
    QCOMPARE(meter->property("warningColor").value<QColor>(), QColor("#8B5C0A"));
    QCOMPARE(meter->property("clippingColor").value<QColor>(), QColor("#B72C3C"));

    auto *model = window.findChild<AudioSourceManager *>();
    const auto first = model->addSource("First", AudioSourceType::Microphone, "missing-first");
    const auto second = model->addSource("Second", AudioSourceType::Microphone, "missing-second");
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    auto *channel = window.findChild<QFrame *>("soundChannel_" + first);
    QVERIFY(channel);
    model->selectSource(first);
    QCoreApplication::processEvents();
    QVERIFY(channel->property("selected").toBool());
    QCOMPARE(channel->palette().color(QPalette::Window), QColor("#DBE9FF"));
    model->selectSource(second);
    QCoreApplication::processEvents();
    QVERIFY(!channel->property("selected").toBool());
    QCOMPARE(channel->palette().color(QPalette::Window), QColor("#F8FAFC"));

    const auto original = qApp->styleSheet();
    const auto restore = qScopeGuard([&] {
        qApp->setStyleSheet(original);
    });
    qApp->setStyleSheet(
        original +
        "\nLevelMeter { qproperty-normalColor: #123456; }"
        "PreviewWidget { qproperty-selectionColor: #654321; qproperty-surroundingColor: #345678; }");
    QCoreApplication::processEvents();
    QCOMPARE(meter->property("normalColor").value<QColor>(), QColor("#123456"));
    if (auto *preview = window.findChild<PreviewWidget *>()) {
        QCOMPARE(preview->selectionColor(), QColor("#654321"));
        QCOMPARE(preview->surroundingColor(), QColor("#345678"));
        QVERIFY(!grabPreview(*preview).isNull());
    }
}

void MainWindowTest::sourceActionsAndSelection()
{
    MainWindow window;

    auto *model = window.findChild<VideoSourceManager *>();
    auto *panel = window.findChild<SourcesPanel *>();
    auto *list = panel->findChild<QListWidget *>("sourceList");
    QVERIFY(!panel->findChild<QToolButton *>("removeSourceButton")->isEnabled());
    QVERIFY(model->addSource({"one", "One"}));
    QVERIFY(model->addSource({"two", "Two"}));
    QCOMPARE(list->count(), 2);
    QVERIFY(model->selectSource("one"));
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), QString("one"));
    list->setCurrentRow(0);
    QCOMPARE(model->currentSourceId(), list->currentItem()->data(Qt::UserRole).toString());

    auto *item = list->currentItem();
    const auto id = model->currentSourceId();
    auto *row = list->itemWidget(item);
    QPointer<QWidget> retainedRow(row);
    QPointer<QWidget> otherRow(list->itemWidget(list->item(1)));
    QVERIFY(model->setSourceStatus(id, "error", "Device disconnected"));
    QCOMPARE(list->itemWidget(item), retainedRow.data());
    QCOMPARE(list->itemWidget(list->item(1)), otherRow.data());
    QCOMPARE(row->findChild<QLabel *>()->toolTip(), QString("Device disconnected"));
    QCOMPARE(model->currentSourceId(), id);

    row->findChild<QToolButton *>("sourceVisibilityButton")->click();
    QCOMPARE(model->sources().at(0).visible, false);
    row->findChild<QToolButton *>("sourceLockButton")->click();
    QCOMPARE(model->sources().at(0).locked, true);
    panel->findChild<QToolButton *>("moveSourceDownButton")->click();
    QCOMPARE(model->sources().at(1).id, id);
    QCOMPARE(list->currentRow(), 1);
    QVERIFY(model->removeSource(id));
    QCOMPARE(list->count(), 1);
    QVERIFY(!panel->findChild<QLabel *>("emptySourcesLabel")->isVisibleTo(panel));
}

void MainWindowTest::imageDialogAndCancellation()
{
    MainWindow window;
    window.show();

    auto *model = window.findChild<VideoSourceManager *>();
    QTemporaryDir directory;
    QImage image(128, 128, QImage::Format_RGB32);
    image.fill(Qt::cyan);

    const auto path = directory.filePath("image.png");
    QVERIFY(image.save(path));

    auto open = [&]() -> QDialog * {
        window.findChild<QToolButton *>("addSourceButton")->click();

        auto *dialog = window.findChild<QDialog *>("sourceAddDialog");
        dialog->findChild<QComboBox *>("sourceTypeInput")->setCurrentIndex(2);
        return dialog;
    };
    QPointer<QDialog> dialog = open();
    QVERIFY(dialog);
    dialog->findChild<QLineEdit *>("sourceNameInput")->setText("Picture");
    dialog->findChild<QLineEdit *>("sourceFileInput")->setText(directory.filePath("missing.png"));
    dialog->findChild<QPushButton *>("confirmSourceAddButton")->click();
    QTRY_VERIFY(!dialog->findChild<QLabel *>("sourceAddError")->text().isEmpty());
    QCOMPARE(model->sources().size(), 0);
    dialog->findChild<QLineEdit *>("sourceFileInput")->setText(path);
    dialog->findChild<QPushButton *>("confirmSourceAddButton")->click();
    QTRY_COMPARE(model->sources().size(), 1);
    QTRY_VERIFY(dialog.isNull());
    dialog = open();
    dialog->findChild<QLineEdit *>("sourceNameInput")->setText("Cancelled");
    dialog->findChild<QLineEdit *>("sourceFileInput")->setText(path);
    dialog->findChild<QPushButton *>("confirmSourceAddButton")->click();
    QTest::keyClick(dialog, Qt::Key_Escape);
    QTRY_VERIFY(dialog.isNull());
    QTest::qWait(100);
    QCOMPARE(model->sources().size(), 1);
}

void MainWindowTest::removalAndFilters()
{
    MainWindow window;
    window.show();

    auto *model = window.findChild<VideoSourceManager *>();
    QVERIFY(model->addSource({"one", "<b>Literal</b>"}));
    QVERIFY(model->selectSource("one"));
    window.findChild<QToolButton *>("removeSourceButton")->click();
    QPointer<QDialog> removal = window.findChild<QDialog *>("sourceRemovalDialog");
    QVERIFY(removal);
    QCOMPARE(removal->findChild<QLabel *>("sourceRemovalMessage")->textFormat(), Qt::PlainText);
    QTest::keyClick(removal, Qt::Key_Escape);
    QTRY_VERIFY(removal.isNull());
    QCOMPARE(model->sources().size(), 1);
    window.findChild<QToolButton *>("sourceFiltersButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>("sourceFiltersDialog");
    QVERIFY(dialog);
    dialog->findChild<QToolButton *>("addFilterButton")->click();
    QCOMPARE(model->sourceFilters("one").size(), 1);
    dialog->findChild<QDoubleSpinBox *>("brightnessInput")->setValue(.45);
    QCOMPARE(model->sourceFilters("one")[0].brightness, double(float(.45)));

    auto *list = dialog->findChild<QListWidget *>("filterList");
    list->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(model->sourceFilters("one")[0].enabled, false);
    dialog->findChild<QToolButton *>("addFilterButton")->click();
    QCOMPARE(model->sourceFilters("one").size(), 2);

    const auto second = model->sourceFilters("one")[1].id;
    dialog->findChild<QToolButton *>("upFilterButton")->click();
    QCOMPARE(model->sourceFilters("one")[0].id, second);
    dialog->findChild<QSlider *>("contrastSlider")->setValue(150);
    QCOMPARE(model->sourceFilters("one")[0].contrast, 1.5);
    dialog->findChild<QPushButton *>("resetFilterButton")->click();
    QCOMPARE(model->sourceFilters("one")[0].contrast, 1.);
    dialog->close();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->sourceFilters("one").size(), 2);
    window.findChild<QToolButton *>("sourceFiltersButton")->click();
    dialog = window.findChild<QDialog *>("sourceFiltersDialog");
    QVERIFY(model->removeSource("one"));
    QTRY_VERIFY(dialog.isNull());
}

void MainWindowTest::disconnectedDeviceDrafts()
{
    MainWindow window;

    auto *model = window.findChild<VideoSourceManager *>();
    QVERIFY(model->addSource(
        {"display", "Missing", DisplaySourceDetails{{"unavailable-display", false}}}));
    QVERIFY(model->selectSource("display"));
    window.findChild<QToolButton *>("sourcePropertiesButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>("displayPropertiesDialog");
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<QComboBox *>("displayTargetInput")->currentData().toString(),
             QString("unavailable-display"));
    QVERIFY(!dialog->findChild<QCheckBox *>("captureCursorInput")->isChecked());
    dialog->findChild<QCheckBox *>("captureCursorInput")->setChecked(true);
    dialog->reject();
    QTRY_VERIFY(dialog.isNull());
    QVERIFY(!model->displaySource("display")->config.captureCursor);
    CameraConfig config;
    config.deviceId = "unavailable-camera";
    config.requestedResolution = QSize(640, 480);
    config.requestedFrameRateNumerator = 30;
    QVERIFY(model->addSource({"camera", "Missing camera", CameraSourceDetails{config}}));
    QVERIFY(model->selectSource("camera"));
    window.findChild<QToolButton *>("sourcePropertiesButton")->click();
    dialog = window.findChild<QDialog *>("cameraPropertiesDialog");
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<QComboBox *>("cameraDeviceInput")->currentData().toString(),
             config.deviceId);
    QTRY_VERIFY(dialog->findChild<QComboBox *>("cameraModeInput")->isEnabled());
    QCOMPARE(
        dialog->findChild<QComboBox *>("cameraModeInput")->currentData().value<CameraMode>().width,
        640);
    QVERIFY(model->removeSource("camera"));
    QTRY_VERIFY(dialog.isNull());
}

void MainWindowTest::audioControlsAndDialogs()
{
    MainWindow window;
    window.show();

    auto *model = window.findChild<AudioSourceManager *>();
    const auto id = model->addSource("Mic", AudioSourceType::Microphone, "unavailable-mic");
    QVERIFY(!id.isEmpty());

    auto *slider = window.findChild<QSlider *>(id + "VolumeSlider");
    QVERIFY(slider);
    QPointer<QWidget> retainedChannel(window.findChild<QWidget *>("soundChannel_" + id));
    QVERIFY(retainedChannel);
    slider->setValue(37);
    QCOMPARE(model->source(id)->volume, double(float(.37)));
    QCOMPARE(model->currentSourceId(), id);

    auto *mute = window.findChild<QToolButton *>(id + "MuteButton");
    QVERIFY(mute);
    mute->click();
    QVERIFY(model->source(id)->muted);
    QTRY_COMPARE_WITH_TIMEOUT(model->source(id)->state, QString("error"), 3000);
    model->sourceLevelsChanged(id);
    QCOMPARE(window.findChild<QWidget *>("soundChannel_" + id), retainedChannel.data());
    QCOMPARE(model->currentSourceId(), id);
    window.findChild<QToolButton *>("soundSourcePropertiesButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>("audioSourceDialog");
    QVERIFY(dialog);
    QVERIFY(!dialog->findChild<QComboBox *>("soundDeviceInput")->isEnabled());
    dialog->findChild<QLineEdit *>("soundNameInput")->setText("Renamed");
    dialog->findChild<QPushButton *>("confirmSoundSourceButton")->click();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->source(id)->name, QString("Renamed"));
    window.findChild<QToolButton *>("removeSoundSourceButton")->click();
    dialog = window.findChild<QDialog *>("sourceRemovalDialog");
    QVERIFY(dialog);
    dialog->findChild<QPushButton *>("confirmSourceRemovalButton")->click();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->count(), 0);
}

void MainWindowTest::outputDraftsAndLocks()
{
    MainWindow window;
    window.show();

    auto *settings = window.findChild<OutputSettings *>();
    const auto directory = settings->recordingDirectory();
    auto open = [&]() {
        window.findChild<QPushButton *>("settingsButton")->click();
        return window.findChild<QDialog *>("outputSettingsDialog");
    };
    QPointer<QDialog> dialog = open();
    QVERIFY(dialog);
    QCOMPARE(dialog->findChild<QTabWidget *>()->currentIndex(), 1);
    QCOMPARE(dialog->findChild<QLineEdit *>("streamKeyInput")->echoMode(), QLineEdit::Password);
    dialog->findChild<QLineEdit *>("recordingDirectoryInput")->setText("draft");
    dialog->reject();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(settings->recordingDirectory(), directory);
    dialog = open();
    QVERIFY(dialog->grab().save(QCoreApplication::applicationDirPath() +
                                "/widgets-output-settings.png"));
    settings->setRecordingLocked(true);
    QVERIFY(!dialog->findChild<QLineEdit *>("recordingDirectoryInput")->isEnabled());
    QVERIFY(dialog->findChild<QLineEdit *>("streamServerInput")->isEnabled());
    settings->setStreamingLocked(true);
    QVERIFY(!dialog->findChild<QLineEdit *>("streamServerInput")->isEnabled());
    settings->setRecordingLocked(false);
    settings->setStreamingLocked(false);
    dialog->findChild<QLineEdit *>("streamServerInput")->setText("invalid");
    dialog->findChild<QPushButton *>("applyOutputSettingsButton")->click();
    QVERIFY(!dialog->findChild<QLabel *>("outputSettingsError")->text().isEmpty());
    QCOMPARE(settings->streamServer(), QString());
    dialog->findChild<QLineEdit *>("streamServerInput")->setText("rtmp://localhost/live");
    dialog->findChild<QLineEdit *>("streamKeyInput")->setText("key");
    dialog->findChild<QSpinBox *>("recordingBitrateInput")->setValue(16000);
    dialog->findChild<QPushButton *>("applyOutputSettingsButton")->click();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(settings->recordingBitrateKbps(), 16000);
    QCOMPARE(settings->streamKey(), QString("key"));
}

void MainWindowTest::scrollingAndKeyboard()
{
    VideoSourceManager model;
    SourcesPanel panel(&model);
    panel.resize(240, 160);
    panel.show();
    for (int n = 0; n < 30; ++n)
        QVERIFY(model.addSource({QString::number(n), QString::number(n)}));

    auto *list = panel.findChild<QListWidget *>();
    QTRY_VERIFY(list->verticalScrollBar()->maximum() > 0);
    list->setFocus();
    list->setCurrentRow(0);
    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(model.currentSourceId(), list->currentItem()->data(Qt::UserRole).toString());
    AudioSourceManager audio;
    MixerPanel mixer(&audio);
    mixer.resize(220, 160);
    mixer.show();
    for (int n = 0; n < 4; ++n)
        QVERIFY(!audio
                     .addSource(QString::number(n), AudioSourceType::Microphone,
                                "missing-" + QString::number(n))
                     .isEmpty());
    QTRY_VERIFY(mixer.findChild<QScrollArea *>()->horizontalScrollBar()->maximum() > 0);
}

void MainWindowTest::aggregatesIndependentSourceControllers()
{
    MainWindow window;

    auto *model = window.findChild<VideoSourceManager *>();
    auto *aggregator = window.findChild<VideoSourceAggregator *>();
    auto *images = window.findChild<ImageSourceController *>();
    auto *display = window.findChild<DisplaySourceController *>();
    auto *camera = window.findChild<CameraSourceController *>();
    QVERIFY(model && aggregator && images && display && camera);
    QList<PreviewSource> output, imageOutput, displayOutput, cameraOutput;

    connect(aggregator, &VideoSourceAggregator::sourcesChanged, &window, [&](const auto &sources) {
        output = sources;
    });
    connect(images, &ImageSourceController::sourcesChanged, &window, [&](const auto &sources) {
        imageOutput = sources;
    });
    connect(display, &DisplaySourceController::sourcesChanged, &window, [&](const auto &sources) {
        displayOutput = sources;
    });
    connect(camera, &CameraSourceController::sourcesChanged, &window, [&](const auto &sources) {
        cameraOutput = sources;
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage pixels(32, 16, QImage::Format_RGB32);
    pixels.fill(Qt::red);

    const auto path = directory.filePath("image.png");
    QVERIFY(pixels.save(path));
    QVERIFY(model->addSource({"image", "Image", ImageSourceDetails{ImageConfig{path}}}));
    VideoSource screen{"display", "Display",
                       DisplaySourceDetails{DisplayConfig{"missing-display"}}};
    screen.visible = false; // Exercise wiring without opening physical devices.
    QVERIFY(model->addSource(screen));
    VideoSource webcam{"camera", "Camera", CameraSourceDetails{CameraConfig{"missing-camera"}}};
    webcam.visible = false;
    QVERIFY(model->addSource(webcam));
    QTRY_COMPARE(output.size(), 3);
    QCOMPARE(imageOutput.size(), 1);
    QCOMPARE(displayOutput.size(), 1);
    QCOMPARE(cameraOutput.size(), 1);
    QCOMPARE(imageOutput[0].id, QString("image"));
    QCOMPARE(displayOutput[0].id, QString("display"));
    QCOMPARE(cameraOutput[0].id, QString("camera"));
    QCOMPARE(output[0].id, QString("camera"));
    QCOMPARE(output[1].id, QString("display"));
    QCOMPARE(output[2].id, QString("image"));
    QTRY_COMPARE(output[2].image, pixels);
    QVERIFY(!output[0].visible && !output[1].visible);
    QVERIFY(model->moveSource("image", -1));
    QVERIFY(model->moveSource("image", -1));
    QTRY_COMPARE(output[0].id, QString("image"));
    QVERIFY(model->removeSource("image"));
    QTRY_COMPARE(output.size(), 2);
    QCOMPARE(output[0].id, QString("camera"));
    QCOMPARE(output[1].id, QString("display"));
}

void MainWindowTest::previewEditsModelAndSynchronizesSelection()
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
        return model->sources().at(0).transform;
    };

    const auto canvasPoint = [&](const QPointF &point) {
        return SourceGeometry::canvasToWidget(preview->size()).map(point).toPoint();
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
    const SourceTransform before = currentTransform();
    const QPoint from = canvasPoint(before.center), to = from + QPoint(30, 15);
    move(from, to);

    const auto inverse = SourceGeometry::canvasToWidget(preview->size()).inverted();
    const auto expectedCenter =
        before.center + inverse.map(QPointF(to)) - inverse.map(QPointF(from));
    QVERIFY(std::hypot(currentTransform().center.x() - expectedCenter.x(),
                       currentTransform().center.y() - expectedCenter.y()) < 1e-6);

    const auto moved = currentTransform();
    window.resize(qMax(window.minimumWidth(), window.width() - 40), window.height());
    QCoreApplication::processEvents();
    QVERIFY(currentTransform() == moved);
    move(canvasPoint(moved.center), canvasPoint(moved.center) + QPoint(10, 5));
    QVERIFY(currentTransform().center != moved.center);
    SourceTransform rotated = currentTransform();
    rotated.rotation = 30;
    rotated.size = QSizeF(450, 250);
    QVERIFY(model->setSourceTransform(frontId, rotated));

    const auto frame = grabPreview(*preview);
    const auto center = canvasPoint(rotated.center);
    QCOMPARE(frame.pixelColor(qRound(center.x() * preview->devicePixelRatioF()),
                              qRound(center.y() * preview->devicePixelRatioF())),
             QColor(Qt::blue));
    QVERIFY(model->setSourceLocked(frontId, true));
    QVERIFY(!model->setSourceTransform(frontId, before));
    QCoreApplication::processEvents(); // Aggregator publishes model changes through a queued
                                       // connection.
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
    const auto corner = canvasPoint(SourceGeometry::sourceToCanvas(rotated).map(QPointF()));
    QCOMPARE(overlayFrame.pixelColor(qRound(corner.x() * preview->devicePixelRatioF()),
                                     qRound(corner.y() * preview->devicePixelRatioF())),
             QColor("#245FBB"));
    QVERIFY(overlayFrame.save(QCoreApplication::applicationDirPath() + "/preview-transforms.png"));

    QCOMPARE(window.findChild<QStackedWidget *>("previewPages")->currentWidget(), preview);
    QVERIFY(preview->lastError().isEmpty());
    QVERIFY(window.close());
}

void MainWindowTest::previewShowsRenderingError()
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
    const QString details = QStringLiteral("<b>测试渲染错误</b>");
    preview->renderingFailed(details);
    QCOMPARE(pages->currentWidget(), preview);
    QTRY_COMPARE(pages->currentWidget(), error);
    QVERIFY(error->isVisible());
    QVERIFY(!preview->isVisible());
    QCOMPARE(error->textFormat(), Qt::PlainText);
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + details);

    QTRY_COMPARE(error->palette().color(QPalette::WindowText), QColor("#B72C3C"));
    QTRY_COMPARE(error->palette().color(QPalette::Window), QColor("#F8FAFC"));
    QCOMPARE(error->text(), QStringLiteral("预览不可用\n") + details);
    QCOMPARE(pages->currentWidget(), error);
    QVERIFY(window.close());
}

void MainWindowTest::dockTitlesStayVerticallyCentered()
{
    // Observe the alignment passed to Qt's text renderer, without depending on
    // font glyph pixels or title-bar dimensions at a particular DPI.
    class TitleStyle final : public QProxyStyle
    {
    public:
        explicit TitleStyle(QStyle *base) : QProxyStyle(base)
        {
        }

        QStringList titles;
        mutable QMap<QString, int> alignments;

        void drawItemText(QPainter *painter, const QRect &rect, int flags, const QPalette &palette,
                          bool enabled, const QString &text,
                          QPalette::ColorRole role = QPalette::NoRole) const override
        {
            if (titles.contains(text))
                alignments.insert(text, flags);
            QProxyStyle::drawItemText(painter, rect, flags, palette, enabled, text, role);
        }
    };

    auto *base = QStyleFactory::create("Fusion");
    QVERIFY(base);
    TitleStyle style(base);
    MainWindow window;
    window.show();

    const auto docks = window.findChildren<QDockWidget *>();
    QCOMPARE(docks.size(), 3);
    for (auto *dock : docks) {
        QVERIFY(!dock->titleBarWidget());
        style.titles.append(dock->windowTitle());
        dock->setStyle(&style);
    }
    {

        style.alignments.clear();
        for (auto *dock : docks) {
            QVERIFY(!dock->grab().isNull());
            QVERIFY(style.alignments.contains(dock->windowTitle()));
            const int flags = style.alignments.value(dock->windowTitle());
            QCOMPARE(flags & int(Qt::AlignVertical_Mask), int(Qt::AlignVCenter));
            QCOMPARE(flags & int(Qt::AlignHorizontal_Mask), int(Qt::AlignLeft));
            QVERIFY(!dock->titleBarWidget());
        }
    }
}

void MainWindowTest::keepsOutputStatusVisibleWhenResized()
{
    MainWindow window;

    auto *output = window.findChild<QLabel *>(QStringLiteral("outputStatus"));
    QVERIFY(output);
    QCOMPARE(output->text(), QStringLiteral("录制 00:00:00   |   直播 00:00:00"));
    QVERIFY(!window.findChild<QLabel *>(QStringLiteral("outputMetrics")));
    window.show();

    for (const auto &size : {QSize(1280, 800), window.minimumSize(), QSize(1280, 800)}) {
        window.resize(size);
        window.statusBar()->showMessage(QStringLiteral("Temporary status message"));
        QTRY_VERIFY(output->isVisible());
        QTRY_VERIFY(output->width() >= output->sizeHint().width());
        const QPoint position = output->mapTo(window.statusBar(), QPoint(0, 0));
        QVERIFY(window.statusBar()->rect().contains(QRect(position, output->size())));
    }
}

void MainWindowTest::recordingAcceptance()
{
    if (qEnvironmentVariableIntValue("OPENLIVE_RECORDING_SOAK_SECONDS") <= 0)
        QSKIP("Explicit real-time recording acceptance only");
    MainWindow window;
    RecordingAcceptance::run(window);
}

void MainWindowTest::cameraHardwarePreview()
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
    const QString id = controller->addCamera("Camera", CameraConfig{devices[0].id});
    QVERIFY(!id.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(model->cameraSource(id)->runtime.captureState, QString("capturing"),
                              15000);

    const auto filter = model->addFilter(id);
    QVERIFY(model->setFilterParameters(id, filter, 0, 1, 0));
    QTest::qWait(500);
    QVERIFY(preview->lastError().isEmpty());
    QVERIFY(failures.isEmpty());

    const auto frame = grabPreview(*preview);
    QVERIFY(!frame.isNull());
    const QColor pixel = frame.pixelColor(frame.width() / 2, frame.height() / 2);
    QVERIFY(qAbs(pixel.red() - pixel.green()) <= 3 && qAbs(pixel.green() - pixel.blue()) <= 3);
    // Keep the mouse pressed while real camera frames arrive between edits.
    // Check the entire model -> controller -> preview -> model path.
    const SourceTransform start{{960, 540}, {600, 300}, 0};
    for (int operation = 0; operation < 3; ++operation) {
        QVERIFY(model->setSourceTransform(id, start));
        QTest::qWait(100);

        const auto display = SourceGeometry::canvasToWidget(preview->size());
        const auto center = display.map(start.center);
        const QPointF from =
            operation == 0 ? center
            : operation == 1
                ? display.map(SourceGeometry::sourceToCanvas(start).map(QPointF(600, 300)))
                : display.map(SourceGeometry::sourceToCanvas(start).map(QPointF(300, 0))) -
                      QPointF(0, 24);
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

        const auto actual = model->sources().at(0).transform;
        if (operation == 0)
            QVERIFY(QLineF(actual.center, QPointF(1080, 600)).length() < 0.001);
        else if (operation == 1) {
            QVERIFY(qAbs(actual.size.width() - 660) < 0.001);
            QVERIFY(qAbs(actual.size.height() - 330) < 0.001);
        } else
            QVERIFY(qAbs(actual.rotation - 30) < 0.001);
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
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(model->setSourceVisible(id, false));
    QTRY_COMPARE_WITH_TIMEOUT(controller->activeWorkerCount(), 0, 10000);
    QVERIFY(model->removeSource(id));
}

void MainWindowTest::recordingControlsAndGracefulClose()
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
    QTRY_COMPARE_WITH_TIMEOUT(recorder->state(), QString("recording"), 5000);

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
    QCOMPARE(recorder->state(), QString("recording"));
    QVERIFY(!window.close());
    QTRY_VERIFY_WITH_TIMEOUT(!recorder->busy(), 10000);
    QTRY_VERIFY(!window.isVisible());
    QVERIFY2(recorder->lastError().isEmpty(), qPrintable(recorder->lastError()));
    QVERIFY(recorder->fileComplete());
    QVERIFY(QFileInfo(recorder->filePath()).size() > 1000);
    QCOMPARE(output->text(), QStringLiteral("录制 00:00:00   |   直播 00:00:00"));
    QVERIFY(!settings->recordingLocked());
}

void MainWindowTest::streamingControlsAndGracefulClose()
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
    QTRY_COMPARE_WITH_TIMEOUT(stream->state(), QString("reconnecting"), 5000);
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
    QTRY_COMPARE_WITH_TIMEOUT(recording->state(), QString("recording"), 5000);
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

void MainWindowTest::previewDisplaysImageSources()
{
    MainWindow window;

    auto *preview = window.findChild<PreviewWidget *>("previewWidget");
    if (!preview)
        QSKIP("Native desktop OpenGL is required for preview integration testing.");
    window.resize(1100, 820); // Exercise letterboxing with a deliberately non-16:9 viewport.
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
    QTRY_VERIFY(!sources->sources().at(0).visible);
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    click(visibility);
    QTRY_VERIFY(sources->sources().at(0).visible);
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    click(down);
    QTRY_COMPARE(sources->sources().at(1).id, overlayId);
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::white));
    QVERIFY(sources->moveSource(overlayId, -1));
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::yellow));
    QVERIFY(sources->removeSource(overlayId));
    verifyFrame();
    QVERIFY(sources->removeSource(sources->sources().at(0).id));
    QTRY_COMPARE(sampleCanvas(.6, .6), QColor(Qt::black));
    QVERIFY(window.close());
}

int main(int argc, char **argv)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    QFile style(":/assets/styles/style.qss");
    if (!style.open(QIODevice::ReadOnly | QIODevice::Text))
        qFatal("Cannot load application style sheet");
    application.setStyleSheet(QString::fromUtf8(style.readAll()));
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_mainwindow.moc"
