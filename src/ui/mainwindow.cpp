#include "ui/mainwindow.h"
#include "ui/outputwidgets.h"
#include "ui/sourcewidgets.h"
#include "ui/audiowidgets.h"
#include <QPushButton>
#include "video/videosourcemodel.h"
#include "video/videosourceaggregator.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"
#include "audio/audiosourcemodel.h"
#include "ui/uicolors.h"
#include "ui/previewwidget.h"
#include "output/recordingcontroller.h"
#include "output/streamingcontroller.h"
#include "output/outputsettings.h"
#include <QCloseEvent>
#include <QtAlgorithms>

#include <QApplication>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QStatusBar>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenLive Studio"));
    setMinimumSize(960, 640);
    resize(1280, 720);

    m_videoSourceModel = new VideoSourceModel;
    m_audioSourceModel = new AudioSourceModel;
    m_outputSettings = new OutputSettings;
    m_imageSourceController = new ImageSourceController(*m_videoSourceModel, this);
    m_displaySourceController = new DisplaySourceController(*m_videoSourceModel, this);
    m_cameraSourceController = new CameraSourceController(*m_videoSourceModel, this);
    m_videoSourceAggregator = new VideoSourceAggregator(*m_videoSourceModel, this);
    connect(m_displaySourceController, &DisplaySourceController::sourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setDisplaySources);
    connect(m_imageSourceController, &ImageSourceController::sourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setImageSources);
    connect(m_cameraSourceController, &CameraSourceController::sourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setCameraSources);
    setupPreview();
    if (auto *preview=findChild<PreviewWidget *>(QStringLiteral("previewWidget"))) {
        m_recordingController=new RecordingController(*preview->compositor(),*m_audioSourceModel->mixer(),this);
        m_streamingController=new StreamingController(*preview->compositor(),*m_audioSourceModel->mixer(),this);
        connect(m_streamingController,&StreamingController::statusChanged,this,&MainWindow::updateOutputStatus);
        connect(m_streamingController,&StreamingController::finished,this,[this] { if (m_closePending) QTimer::singleShot(0,this,&QWidget::close); });
        connect(m_recordingController,&RecordingController::overloaded,m_streamingController,&StreamingController::stopForOverload);
        connect(m_recordingController,&RecordingController::statusChanged,this,&MainWindow::updateOutputStatus);
        connect(m_recordingController,&RecordingController::finished,this,[this] {
            if (m_closePending) QTimer::singleShot(0,this,&QWidget::close);
        });
    }
    setupStatusBar();
    connect(m_audioSourceModel, &AudioSourceModel::sourceError, this, [this](const QString &id, const QString &message) {
        statusBar()->showMessage(tr("声音来源“%1”：%2").arg(m_audioSourceModel->source(id).value("name").toString(), message));
    });
    const auto showSourceError = [this](const QString &id, const QString &message) {
        QString name = tr("图片");
        for (int row = 0; row < m_videoSourceModel->rowCount(); ++row) {
            const auto index = m_videoSourceModel->index(row, 0);
            if (index.data(VideoSourceModel::IdRole).toString() == id) {
                name = index.data(VideoSourceModel::NameRole).toString();
                break;
            }
        }
        statusBar()->showMessage(tr("来源“%1”：%2").arg(name, message));
    };
    connect(m_imageSourceController, &ImageSourceController::sourceError, this, showSourceError);
    connect(m_displaySourceController, &DisplaySourceController::sourceError, this, showSourceError);
    connect(m_cameraSourceController, &CameraSourceController::sourceError, this, showSourceError);
    if (auto *preview = findChild<PreviewWidget *>(QStringLiteral("previewWidget")))
        connect(preview, &PreviewWidget::sourceError, this, showSourceError, Qt::QueuedConnection);
    setupSourcesDock();
    setupMixerDock();
    setupControlsDock();
    setupDockLayout();
    // Do not pass `this` when constructing these objects above: Qt deletes child
    // objects in child-list order. Set their parent after creating the docks so
    // the dock widgets are destroyed before the models/settings they reference.
    m_videoSourceModel->setParent(this);
    m_audioSourceModel->setParent(this);
    m_outputSettings->setParent(this);
    applyColors();
}

MainWindow::~MainWindow()
{
    qDeleteAll(findChildren<QDialog *>(QString(),Qt::FindDirectChildrenOnly));
    // Finish encoding while its compositor and audio mixer are still alive.
    delete m_streamingController; m_streamingController=nullptr;
    delete m_recordingController; m_recordingController=nullptr;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if ((m_recordingController && m_recordingController->busy()) || (m_streamingController && m_streamingController->busy())) {
        m_closePending=true;
        if (m_streamingController) m_streamingController->stopStreaming();
        if (m_recordingController) m_recordingController->stopRecording();
        event->ignore(); return;
    }
    m_closePending=false;
    QMainWindow::closeEvent(event);
}

void MainWindow::updateOutputStatus()
{
    if (!m_recordingController || !m_outputStatus) return;
    m_outputSettings->setRecordingLocked(m_recordingController->busy());
    if (m_streamingController) m_outputSettings->setStreamingLocked(m_streamingController->busy());
    const auto formatDuration=[](qint64 ms) {
        return QStringLiteral("%1:%2:%3").arg(ms/3600000,2,10,QLatin1Char('0'))
            .arg(ms/60000%60,2,10,QLatin1Char('0')).arg(ms/1000%60,2,10,QLatin1Char('0'));
    };
    const auto live=m_streamingController ? m_streamingController->status() : StreamingStatus{};
    m_outputStatus->setText(tr("录制 %1   |   直播 %2").arg(
        formatDuration(m_recordingController->busy() ? m_recordingController->durationMs() : 0),
        formatDuration(m_streamingController && m_streamingController->busy() ? live.durationMs : 0)));
    if (!live.error.isEmpty() && live.error!=m_previousStreamingError) statusBar()->showMessage(tr("直播：%1").arg(live.error));
    m_previousStreamingError=live.error;
    if (!m_recordingController->lastError().isEmpty() && m_previousRecordingError!=m_recordingController->lastError())
        statusBar()->showMessage(tr("录制失败：%1%2").arg(m_recordingController->lastError(),
            m_recordingController->fileComplete() ? tr("（已有内容已完成封装）") : tr("（文件未完整完成）")));
    m_previousRecordingError=m_recordingController->lastError();
}

void MainWindow::toggleRecording()
{
    if (!m_recordingController) return;
    if (m_recordingController->busy()) { m_recordingController->stopRecording(); return; }
    const auto path=m_outputSettings->nextRecordingPath();
    if (path.isEmpty()) { statusBar()->showMessage(m_outputSettings->lastError()); return; }
    statusBar()->clearMessage();
    m_recordingController->startRecording(path,m_outputSettings->recordingBitrate());
}

void MainWindow::toggleStreaming()
{
    if (!m_streamingController) return;
    if (m_streamingController->busy()) { m_streamingController->stopStreaming(); return; }
    statusBar()->clearMessage();
    m_streamingController->startStreaming(m_outputSettings->streamServer(),m_outputSettings->streamKey(),m_outputSettings->streamingBitrate());
}

void MainWindow::showOutputSettingsDialog()
{
    if (auto *dialog = findChild<QDialog *>("outputSettingsDialog")) {
        dialog->show(); dialog->raise(); dialog->activateWindow(); return;
    }
    createOutputSettingsDialog(this, m_outputSettings)->open();
}

void MainWindow::applyColors()
{
    const auto color = [](const char *key) { return UiColors::color(QLatin1String(key)); };
    QPalette colors = QApplication::palette();
    colors.setColor(QPalette::Window, color("background.window"));
    colors.setColor(QPalette::WindowText, color("text.primary"));
    colors.setColor(QPalette::Base, color("background.input"));
    colors.setColor(QPalette::AlternateBase, color("background.panel"));
    colors.setColor(QPalette::Text, color("text.primary"));
    colors.setColor(QPalette::PlaceholderText, color("text.secondary"));
    colors.setColor(QPalette::Button, color("button.background.normal"));
    colors.setColor(QPalette::ButtonText, color("text.primary"));
    colors.setColor(QPalette::Highlight, color("background.selected"));
    colors.setColor(QPalette::HighlightedText, color("text.primary"));
    colors.setColor(QPalette::ToolTipBase, color("background.header"));
    colors.setColor(QPalette::ToolTipText, color("text.primary"));
    colors.setColor(QPalette::Link, color("border.focus"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        colors.setColor(QPalette::Disabled, role, color("text.disabled"));
    const auto hex = [&color](const char *key) { return color(key).name(); };
    setStyleSheet(QStringLiteral(
        "QStatusBar { background-color: %1; color: %2; }"
        "QDockWidget { color: %2; }"
        "QDockWidget::title { background-color: %1; text-align: left center; }"
        "QMainWindow::separator { background-color: %4; }"
        "QMainWindow::separator:hover { background-color: %6; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: %2; }"
        "QLabel:disabled { color: %5; }"
        "QLabel[uiError=\"true\"] { color: %7; background-color: %3; }")
        .arg(hex("background.header"), hex("text.primary"), hex("background.panel"),
             hex("border.default"), hex("text.disabled"), hex("border.focus"), hex("status.error")));
    // Style-sheet repolishing restores cached palettes. Apply the new palette
    // afterwards, including child windows (floating docks and dialogs).
    // Keep the application's global palette untouched.
    setPalette(colors);
    for (auto *dock : {m_videoSourceDock, m_audioSourceDock, m_controlDock})
        if (dock) dock->setPalette(colors);
    for (auto *dialog : findChildren<QDialog *>()) dialog->setPalette(colors);
    if (auto *container = findChild<QWidget *>(QStringLiteral("previewContainer"))) {
        container->setStyleSheet(QStringLiteral(
            "QWidget#previewContainer { background-color: %1; }")
            .arg(hex("preview.surrounding")));
    }
    if (auto *preview = findChild<PreviewWidget *>(QStringLiteral("previewWidget"))) {
        preview->setSurroundingColor(color("preview.surrounding"));
        preview->setSelectionColor(color("border.focus"));
    }
}

void MainWindow::showSourceAddDialog()
{
    createSourceAddDialog(this, m_videoSourceModel, m_imageSourceController, m_displaySourceController, m_cameraSourceController)->open();
}

void MainWindow::showAudioSourceAddDialog()
{
    showAudioSourcePropertiesDialog({});
}

void MainWindow::showAudioSourcePropertiesDialog(const QString &sourceId)
{
    if (!sourceId.isEmpty() && m_audioSourceModel->source(sourceId).isEmpty()) return;
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->objectName() == "audioSourceDialog" && dialog->property("audioSourceId").toString() == sourceId) {
            dialog->show(); dialog->raise(); dialog->activateWindow(); return;
        }
    }
    createAudioSourceDialog(this, m_audioSourceModel, sourceId)->open();
}

void MainWindow::showAudioSourceRemovalDialog(const QString &sourceId)
{
    const auto source = m_audioSourceModel->source(sourceId);
    if (source.isEmpty()) return;
    createRemovalDialog(this, source.value("name").toString(), tr("移除后将停止此设备的声音采集。"),
        [this, sourceId] { return m_audioSourceModel->removeSource(sourceId); })->open();
}

void MainWindow::showSourceRemovalDialog(const QString &sourceId)
{
    for (int row = 0; row < m_videoSourceModel->rowCount(); ++row) {
        const auto index = m_videoSourceModel->index(row, 0);
        if (index.data(VideoSourceModel::IdRole).toString() != sourceId) continue;
        createRemovalDialog(this, index.data(VideoSourceModel::NameRole).toString(), tr("仅从来源列表移除，不会删除磁盘上的文件。"),
            [this, sourceId] { return m_videoSourceModel->removeSource(sourceId); })->open();
        return;
    }
}

void MainWindow::showSourceFiltersDialog(const QString &sourceId)
{
    if (!m_videoSourceModel->canFilterSource(sourceId)) return;
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->property("filterSourceId").toString() == sourceId) {
            dialog->show(); dialog->raise(); dialog->activateWindow(); return;
        }
    }
    createSourceFiltersDialog(this, m_videoSourceModel, sourceId)->open();
}

void MainWindow::showSourcePropertiesDialog(const QString &sourceId)
{
    if (m_videoSourceModel->cameraProperties(sourceId).isEmpty() && m_videoSourceModel->displayProperties(sourceId).isEmpty()) return;
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->property("displaySourceId").toString() == sourceId) {
            dialog->show(); dialog->raise(); dialog->activateWindow(); return;
        }
    }
    createSourcePropertiesDialog(this, m_videoSourceModel, sourceId, m_displaySourceController, m_cameraSourceController)->open();
}

void MainWindow::setupPreview()
{
    auto *container = new QWidget(this);
    container->setObjectName(QStringLiteral("previewContainer"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);
    auto *pages = new QStackedWidget(container);
    pages->setObjectName(QStringLiteral("previewPages"));
    auto *errorLabel = new QLabel(pages);
    errorLabel->setObjectName(QStringLiteral("previewErrorLabel"));
    errorLabel->setProperty("uiError", true);
    errorLabel->setTextFormat(Qt::PlainText);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    errorLabel->setMargin(12);
    pages->addWidget(errorLabel);
    layout->addWidget(pages, 1);
    setCentralWidget(container);

    const auto showError = [pages, errorLabel](const QString &details) {
        errorLabel->setText(QObject::tr("预览不可用\n%1").arg(details));
        pages->setCurrentWidget(errorLabel);
    };
    const QString platform = QGuiApplication::platformName();
    if (platform == QStringLiteral("offscreen") || platform == QStringLiteral("minimal")) {
        showError(tr("当前平台不支持 OpenGL 预览。"));
        return;
    }

    auto *preview = new PreviewWidget(pages);
    preview->setObjectName(QStringLiteral("previewWidget"));
    connect(preview, &PreviewWidget::renderingError, pages, showError, Qt::QueuedConnection);
    connect(m_videoSourceAggregator, &VideoSourceAggregator::sourcesChanged, preview, &PreviewWidget::setSources);
    connect(preview, &PreviewWidget::gpuTransferFailed, m_displaySourceController, &DisplaySourceController::failTransfer, Qt::QueuedConnection);
    connect(preview, &PreviewWidget::selectionRequested, m_videoSourceModel, &VideoSourceModel::selectSource);
    connect(preview, &PreviewWidget::transformRequested, m_videoSourceModel, &VideoSourceModel::setSourceTransform);
    connect(m_videoSourceModel, &VideoSourceModel::currentSourceIdChanged, preview, [this, preview] {
        preview->setSelectedSourceId(m_videoSourceModel->currentSourceId());
    });
    preview->setSelectedSourceId(m_videoSourceModel->currentSourceId());
    pages->addWidget(preview);
    pages->setCurrentWidget(preview);
}

void MainWindow::setupStatusBar()
{
    m_outputStatus = new QLabel(
        tr("录制 00:00:00   |   直播 00:00:00"), this);
    m_outputStatus->setObjectName(QStringLiteral("outputStatus"));

    // Output timers remain visible even when a temporary status message appears.
    statusBar()->addPermanentWidget(m_outputStatus);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::setupSourcesDock()
{
    m_videoSourceDock = new QDockWidget(tr("画面来源"), this);
    m_videoSourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_videoSourceDock->setObjectName("sourcesDock");
    m_videoSourceDock->setMinimumWidth(220); m_videoSourceDock->setMaximumWidth(320);
    auto *panel = new SourcesPanel(m_videoSourceModel, m_videoSourceDock);
    m_videoSourceDock->setWidget(panel);
    connect(panel, &SourcesPanel::addRequested, this, &MainWindow::showSourceAddDialog);
    connect(panel, &SourcesPanel::removalRequested, this, &MainWindow::showSourceRemovalDialog);
    connect(panel, &SourcesPanel::filtersRequested, this, &MainWindow::showSourceFiltersDialog);
    connect(panel, &SourcesPanel::propertiesRequested, this, &MainWindow::showSourcePropertiesDialog);
}

void MainWindow::setupMixerDock()
{
    m_audioSourceDock = new QDockWidget(tr("声音来源"), this);
    m_audioSourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_audioSourceDock->setObjectName("mixerDock");
    m_audioSourceDock->setMinimumWidth(220); m_audioSourceDock->setMaximumWidth(320);
    auto *panel = new MixerPanel(m_audioSourceModel, m_audioSourceDock);
    m_audioSourceDock->setWidget(panel);
    connect(panel, &MixerPanel::addRequested, this, &MainWindow::showAudioSourceAddDialog);
    connect(panel, &MixerPanel::removalRequested, this, &MainWindow::showAudioSourceRemovalDialog);
    connect(panel, &MixerPanel::propertiesRequested, this, &MainWindow::showAudioSourcePropertiesDialog);
}

void MainWindow::setupControlsDock()
{
    m_controlDock = new QDockWidget(tr("控制"), this);
    m_controlDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_controlDock->setObjectName(QStringLiteral("controlsDock"));
    m_controlDock->setMinimumHeight(80);
    m_controlDock->setMaximumHeight(180);

    auto *controls = createOutputControls(m_controlDock, m_outputSettings, m_recordingController, m_streamingController);
    m_controlDock->setWidget(controls);
    connect(controls->findChild<QPushButton *>("recordButton"), &QPushButton::clicked, this, &MainWindow::toggleRecording);
    connect(controls->findChild<QPushButton *>("streamButton"), &QPushButton::clicked, this, &MainWindow::toggleStreaming);
    connect(controls->findChild<QPushButton *>("settingsButton"), &QPushButton::clicked, this, &MainWindow::showOutputSettingsDialog);
}

void MainWindow::setupDockLayout()
{
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_videoSourceDock);
    addDockWidget(Qt::LeftDockWidgetArea, m_audioSourceDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_controlDock);
    splitDockWidget(m_videoSourceDock, m_audioSourceDock, Qt::Vertical);
    resizeDocks({m_videoSourceDock, m_audioSourceDock}, {220, 220}, Qt::Horizontal);
    resizeDocks({m_controlDock}, {80}, Qt::Vertical);
    resizeDocks({m_videoSourceDock, m_audioSourceDock}, {height() / 2, height() / 2}, Qt::Vertical);
}
