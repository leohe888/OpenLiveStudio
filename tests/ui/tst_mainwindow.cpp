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

class MainWindowTest : public QObject
{
    Q_OBJECT
private slots:
    void shellAndPalette();
    void styleSheetControlsCustomPainting();
    void sourceActionsAndSelection();
    void imageDialogAndCancellation();
    void removalAndFilters();
    void audioControlsAndDialogs();
    void outputDraftsAndLocks();
    void scrollingAndKeyboard();
    void aggregatesIndependentSourceControllers();
    void dockTitlesStayVerticallyCentered();
    void keepsOutputStatusVisibleWhenResized();
    void cameraFormatSelectionFiltersModes();
    void cameraDefaultSettingsCanBeConfirmed();
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
    QVERIFY(window.findChild<VideoSourcePanel *>());
    QVERIFY(window.findChild<AudioSourcePanel *>());
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
    auto *panel = window.findChild<VideoSourcePanel *>();
    auto *list = panel->findChild<QListWidget *>("sourceList");
    QVERIFY(!panel->findChild<QToolButton *>("removeSourceButton")->isEnabled());
    QVERIFY(addSource(*model, {"one", "One"}));
    QVERIFY(addSource(*model, {"two", "Two"}));
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
    QVERIFY(setSourceStatus(*model, id, VideoSourceState::Error, "Device disconnected"));
    QCOMPARE(list->itemWidget(item), retainedRow.data());
    QCOMPARE(list->itemWidget(list->item(1)), otherRow.data());
    QCOMPARE(row->findChild<QLabel *>()->toolTip(), QString("Device disconnected"));
    QCOMPARE(model->currentSourceId(), id);

    row->findChild<QToolButton *>("sourceVisibilityButton")->click();
    QCOMPARE(model->sources().at(0)->visible(), false);
    row->findChild<QToolButton *>("sourceLockButton")->click();
    QCOMPARE(model->sources().at(0)->locked(), true);
    panel->findChild<QToolButton *>("moveSourceDownButton")->click();
    QCOMPARE(model->sources().at(1)->id(), id);
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
    QVERIFY(addSource(*model, {"one", "<b>Literal</b>"}));
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
    QCOMPARE(std::get<ColorCorrectionConfig>(model->sourceFilters("one")[0].config()).brightness(), double(.45f));

    auto *list = dialog->findChild<QListWidget *>("filterList");
    list->item(0)->setCheckState(Qt::Unchecked);
    QCOMPARE(model->sourceFilters("one")[0].enabled(), false);
    dialog->findChild<QToolButton *>("addFilterButton")->click();
    QCOMPARE(model->sourceFilters("one").size(), 2);

    const auto second = model->sourceFilters("one")[1].id();
    QCOMPARE(model->currentFilterId("one"), second);
    dialog->findChild<QToolButton *>("upFilterButton")->click();
    QCOMPARE(model->sourceFilters("one")[0].id(), second);
    dialog->findChild<QSlider *>("contrastSlider")->setValue(150);
    QCOMPARE(std::get<ColorCorrectionConfig>(model->sourceFilters("one")[0].config()).contrast(), 1.5);
    dialog->findChild<QPushButton *>("resetFilterButton")->click();
    QCOMPARE(std::get<ColorCorrectionConfig>(model->sourceFilters("one")[0].config()).contrast(), 1.);
    dialog->close();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->sourceFilters("one").size(), 2);
    window.findChild<QToolButton *>("sourceFiltersButton")->click();
    dialog = window.findChild<QDialog *>("sourceFiltersDialog");
    QVERIFY(dialog);
    list = dialog->findChild<QListWidget *>("filterList");
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), second);
    const auto first = model->sourceFilters("one")[1].id();
    QVERIFY(model->selectFilter("one", first));
    QCOMPARE(list->currentItem()->data(Qt::UserRole).toString(), first);
    list->setCurrentRow(0);
    QCOMPARE(model->currentFilterId("one"), second);
    QVERIFY(model->selectFilter("one", {}));
    QVERIFY(!list->currentItem());
    QVERIFY(!dialog->findChild<QToolButton *>("upFilterButton")->isEnabled());
    QVERIFY(!dialog->findChild<QDoubleSpinBox *>("brightnessInput")->isEnabled());
    QVERIFY(model->selectFilter("one", second));
    QVERIFY(model->removeFilter("one", second));
    QVERIFY(!list->currentItem());
    QVERIFY(model->currentFilterId("one").isEmpty());
    QVERIFY(model->removeSource("one"));
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
    window.findChild<QToolButton *>("addSoundSourceButton")->click();
    QPointer<QDialog> dialog = window.findChild<QDialog *>("audioSourceAddDialog");
    QVERIFY(dialog);
    QCOMPARE(dialog->windowTitle(), QString::fromUtf8("添加声音来源"));
    QVERIFY(dialog->findChild<QComboBox *>("soundTypeInput")->isEnabled());
    QVERIFY(dialog->findChild<QPushButton *>("refreshSoundDevicesButton"));
    dialog->findChild<QPushButton *>("cancelSoundSourceButton")->click();
    QTRY_VERIFY(dialog.isNull());
    QCOMPARE(model->source(id)->name, QString("Mic"));
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
    VideoSourcePanel panel(&model);
    panel.resize(240, 160);
    panel.show();
    for (int n = 0; n < 30; ++n)
        QVERIFY(addSource(model, {QString::number(n), QString::number(n)}));

    auto *list = panel.findChild<QListWidget *>();
    QTRY_VERIFY(list->verticalScrollBar()->maximum() > 0);
    list->setFocus();
    list->setCurrentRow(0);
    QTest::keyClick(list, Qt::Key_Down);
    QCOMPARE(list->currentRow(), 1);
    QCOMPARE(model.currentSourceId(), list->currentItem()->data(Qt::UserRole).toString());
    AudioSourceManager audio;
    AudioSourcePanel mixer(&audio);
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
    QList<RenderSourceSnapshot> output, imageOutput, displayOutput, cameraOutput;

    connect(aggregator, &VideoSourceAggregator::renderSourcesChanged, &window, [&](const auto &sources) {
        output = sources;
    });
    connect(images, &ImageSourceController::renderSourcesChanged, &window, [&](const auto &sources) {
        imageOutput = sources;
    });
    connect(display, &DisplaySourceController::renderSourcesChanged, &window, [&](const auto &sources) {
        displayOutput = sources;
    });
    connect(camera, &CameraSourceController::renderSourcesChanged, &window, [&](const auto &sources) {
        cameraOutput = sources;
    });
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage pixels(32, 16, QImage::Format_RGB32);
    pixels.fill(Qt::red);

    const auto path = directory.filePath("image.png");
    QVERIFY(pixels.save(path));
    QVERIFY(addSource(*model, {"image", "Image", path}));
    DisplaySource screen{"display", "Display", true};
    screen.setVisible(false); // 隐藏来源以验证信号连接，同时避免打开实际设备。
    QVERIFY(addSource(*model, screen));
    CameraSource webcam{"camera", "Camera", "missing-camera"};
    webcam.setVisible(false);
    QVERIFY(addSource(*model, webcam));
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

void MainWindowTest::dockTitlesStayVerticallyCentered()
{
    // 检查传给 Qt 的对齐参数，避免断言依赖字体像素或特定 DPI 下的标题栏尺寸。
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

void MainWindowTest::cameraFormatSelectionFiltersModes()
{
    MainWindow window;
    auto *cameras = window.findChild<CameraSourceController *>();
    cameras->m_refreshing = true; // 阻止真实设备查询覆盖这个确定的能力列表。
    cameras->m_devices = {{"test-camera", "Test camera"}};
    CameraMode mjpeg{1280, 720, 30, 1, "mjpeg", "1280 × 720 · 30 FPS"};
    CameraMode yuy2{640, 480, 30, 1, "yuyv422", "640 × 480 · 30 FPS"};
    CameraMode unsupported{1920, 1080, 30, 1, "p010le", "1920 × 1080 · 30 FPS"};
    unsupported.supported = false;
    cameras->m_deviceModes.insert("test-camera", {mjpeg, yuy2, unsupported});
    window.findChild<QToolButton *>("addSourceButton")->click();
    auto *dialog = window.findChild<QDialog *>("sourceAddDialog");
    QVERIFY(dialog);
    dialog->findChild<QComboBox *>("sourceTypeInput")->setCurrentIndex(1);
    auto *format = dialog->findChild<QComboBox *>("cameraFormatInput");
    auto *mode = dialog->findChild<QComboBox *>("cameraModeInput");
    QVERIFY(format && mode);
    QCOMPARE(format->currentData().toString(), QString());
    QCOMPARE(mode->count(), 3);
    format->setCurrentIndex(format->findData("mjpeg"));
    QCOMPARE(mode->count(), 2);
    mode->setCurrentIndex(1);
    QCOMPARE(cameraForMode(mode->currentData().value<CameraMode>(), "test-camera").pixelFormat(),
             QString("mjpeg"));
    format->setCurrentIndex(format->findData("yuyv422"));
    QCOMPARE(mode->currentIndex(), 0);
    QCOMPARE(mode->count(), 2);
    QCOMPARE(cameraForMode(mode->currentData().value<CameraMode>(), "test-camera").pixelFormat(),
             QString("yuyv422"));
    QVERIFY(cameraForMode(mode->currentData().value<CameraMode>(), "test-camera")
                .resolution()
                .isEmpty());
    const int row = format->findData("p010le");
    QVERIFY(row >= 0);
    QVERIFY(!(format->model()->flags(format->model()->index(row, 0)) & Qt::ItemIsEnabled));
    dialog->reject();
}

void MainWindowTest::cameraDefaultSettingsCanBeConfirmed()
{
    MainWindow window;
    auto *cameras = window.findChild<CameraSourceController *>();
    auto *manager = window.findChild<VideoSourceManager *>();
    cameras->m_refreshing = true;
    cameras->m_devices = {{"missing-camera", "Test camera"}};
    cameras->m_deviceModes.insert("missing-camera", {{1280, 720, 30, 1, "mjpeg"}});
    window.findChild<QToolButton *>("addSourceButton")->click();
    auto *dialog = window.findChild<QDialog *>("sourceAddDialog");
    QVERIFY(dialog);
    dialog->findChild<QComboBox *>("sourceTypeInput")->setCurrentIndex(1);
    auto *confirm = dialog->findChild<QPushButton *>("confirmSourceAddButton");
    QVERIFY(confirm->isEnabled());
    confirm->click();
    QCOMPARE(dialog->result(), int(QDialog::Accepted));
    QCOMPARE(manager->sources().size(), 1);
    const QString id = manager->sources().first()->id();
    const auto *config = cameraSource(*manager, id);
    QVERIFY(config);
    QVERIFY(config->pixelFormat().isEmpty());
    QVERIFY(config->resolution().isEmpty());
    QCOMPARE(config->frameRateNumerator(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(manager->sourceState(id), VideoSourceState::Error, 5000);
    QVERIFY(!manager->sourceError(id).isEmpty());
}

int main(int argc, char **argv)
{
    return runMainWindowTests<MainWindowTest>(argc, argv);
}

#include "tst_mainwindow.moc"
