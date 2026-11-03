#include "ui/mainwindow.h"
#include "ui/outputwidgets.h"
#include "ui/videosourcepanel.h"
#include "ui/audiosourcepanel.h"
#include "video/videosourcemanager.h"
#include "video/videosourceaggregator.h"
#include "video/image/imagesourcecontroller.h"
#include "video/display/displaysourcecontroller.h"
#include "video/camera/camerasourcecontroller.h"
#include "audio/audiosourcemanager.h"
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
#include <QPushButton>
#include <QStatusBar>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("OpenLive Studio"));
    setMinimumSize(960, 640);
    resize(1280, 720);

    setupSourceControllers();
    setupPreview();
    setupOutputControllers();
    setupStatusBar();
    connectSourceErrors();
    setupVideoSourcesDock();
    setupAudioSourcesDock();
    setupControlsDock();
    setupDockLayout();
    // Qt 按子对象顺序销毁对象；在面板创建后才为管理器和设置指定父对象，
    // 保证面板先销毁，避免析构时访问已经释放的依赖。
    m_videoSourceManager->setParent(this);
    m_audioSourceManager->setParent(this);
    m_outputSettings->setParent(this);
}

void MainWindow::setupSourceControllers()
{
    m_videoSourceManager = new VideoSourceManager;
    m_audioSourceManager = new AudioSourceManager;
    m_outputSettings = new OutputSettings;
    m_imageSourceController = new ImageSourceController(*m_videoSourceManager, this);
    m_displaySourceController = new DisplaySourceController(*m_videoSourceManager, this);
    m_cameraSourceController = new CameraSourceController(*m_videoSourceManager, this);
    m_videoSourceAggregator = new VideoSourceAggregator(*m_videoSourceManager, this);
    connect(m_displaySourceController, &DisplaySourceController::renderSourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setDisplaySources);
    connect(m_imageSourceController, &ImageSourceController::renderSourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setImageSources);
    connect(m_cameraSourceController, &CameraSourceController::renderSourcesChanged,
            m_videoSourceAggregator, &VideoSourceAggregator::setCameraSources);
}

void MainWindow::setupOutputControllers()
{
    if (auto *preview = m_preview) {
        m_recordingController =
            new RecordingController(*preview->compositor(), *m_audioSourceManager->mixer(), this);
        m_streamingController =
            new StreamingController(*preview->compositor(), *m_audioSourceManager->mixer(), this);
        connect(m_streamingController, &StreamingController::statusChanged, this,
                &MainWindow::updateOutputStatus);
        connect(m_streamingController, &StreamingController::finished, this, [this] {
            if (m_closePending)
                QTimer::singleShot(0, this, &QWidget::close);
        });
        connect(m_recordingController, &RecordingController::overloaded, m_streamingController,
                &StreamingController::stopForOverload);
        connect(m_recordingController, &RecordingController::statusChanged, this,
                &MainWindow::updateOutputStatus);
        connect(m_recordingController, &RecordingController::finished, this, [this] {
            if (m_closePending)
                QTimer::singleShot(0, this, &QWidget::close);
        });
    }
}

void MainWindow::connectSourceErrors()
{
    connect(m_audioSourceManager, &AudioSourceManager::sourceFailed, this,
            [this](const QString &sourceId, const QString &message) {
                const auto *source = m_audioSourceManager->source(sourceId);
                statusBar()->showMessage(
                    tr("声音来源“%1”：%2").arg(source ? source->name : sourceId, message));
            });

    const auto showSourceError = [this](const QString &sourceId, const QString &message) {
        QString name = tr("图片");
        const auto sourceName = m_videoSourceManager->sourceName(sourceId);
        if (!sourceName.isNull())
            name = sourceName;
        statusBar()->showMessage(tr("来源“%1”：%2").arg(name, message));
    };

    connect(m_imageSourceController, &ImageSourceController::sourceFailed, this, showSourceError);
    connect(m_displaySourceController, &DisplaySourceController::sourceFailed, this,
            showSourceError);
    connect(m_cameraSourceController, &CameraSourceController::sourceFailed, this, showSourceError);
    if (auto *preview = m_preview)
        connect(preview, &PreviewWidget::sourceFailed, this, showSourceError, Qt::QueuedConnection);
}

MainWindow::~MainWindow()
{
    qDeleteAll(findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly));
    // 编码器仍引用合成器和混音器，必须先完成编码，再释放这两个依赖。
    delete m_streamingController;
    m_streamingController = nullptr;
    delete m_recordingController;
    m_recordingController = nullptr;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if ((m_recordingController && m_recordingController->busy()) ||
        (m_streamingController && m_streamingController->busy())) {
        m_closePending = true;
        if (m_streamingController)
            m_streamingController->stopStreaming();
        if (m_recordingController)
            m_recordingController->stopRecording();
        event->ignore();
        return;
    }
    m_closePending = false;
    QMainWindow::closeEvent(event);
}

void MainWindow::updateOutputStatus()
{
    if (!m_recordingController || !m_outputStatusLabel)
        return;
    m_outputSettings->setRecordingLocked(m_recordingController->busy());
    if (m_streamingController)
        m_outputSettings->setStreamingLocked(m_streamingController->busy());

    const auto formatDuration = [](qint64 ms) {
        return QStringLiteral("%1:%2:%3")
            .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
            .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
            .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
    };

    const auto live = m_streamingController ? m_streamingController->status() : StreamingStatus{};
    m_outputStatusLabel->setText(
        tr("录制 %1   |   直播 %2")
            .arg(formatDuration(m_recordingController->busy() ? m_recordingController->durationMs()
                                                              : 0),
                 formatDuration(m_streamingController && m_streamingController->busy()
                                    ? live.durationMs
                                    : 0)));
    if (!live.error.isEmpty() && live.error != m_previousStreamingError)
        statusBar()->showMessage(tr("直播：%1").arg(live.error));
    m_previousStreamingError = live.error;
    if (!m_recordingController->lastError().isEmpty() &&
        m_previousRecordingError != m_recordingController->lastError())
        statusBar()->showMessage(
            tr("录制失败：%1%2")
                .arg(m_recordingController->lastError(), m_recordingController->fileComplete()
                                                             ? tr("（已有内容已完成封装）")
                                                             : tr("（文件未完整完成）")));
    m_previousRecordingError = m_recordingController->lastError();
}

void MainWindow::toggleRecording()
{
    if (!m_recordingController)
        return;
    if (m_recordingController->busy()) {
        m_recordingController->stopRecording();
        return;
    }

    const auto path = m_outputSettings->nextRecordingPath();
    if (path.isEmpty()) {
        statusBar()->showMessage(m_outputSettings->lastError());
        return;
    }
    statusBar()->clearMessage();
    m_recordingController->startRecording(path, m_outputSettings->recordingBitrateKbps());
}

void MainWindow::toggleStreaming()
{
    if (!m_streamingController)
        return;
    if (m_streamingController->busy()) {
        m_streamingController->stopStreaming();
        return;
    }
    statusBar()->clearMessage();
    m_streamingController->startStreaming(m_outputSettings->streamServer(),
                                          m_outputSettings->streamKey(),
                                          m_outputSettings->streamingBitrateKbps());
}

void MainWindow::showOutputSettingsDialog()
{
    if (auto *dialog = findChild<QDialog *>("outputSettingsDialog")) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    createOutputSettingsDialog(this, m_outputSettings)->open();
}

void MainWindow::showSourceAddDialog()
{
    createSourceAddDialog(this, m_videoSourceManager, m_imageSourceController,
                          m_displaySourceController, m_cameraSourceController)
        ->open();
}

void MainWindow::showAudioSourceAddDialog()
{
    if (auto *dialog = findChild<QDialog *>("audioSourceAddDialog")) {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    createAudioSourceAddDialog(this, m_audioSourceManager)->open();
}

void MainWindow::showAudioSourceRemovalDialog(const QString &sourceId)
{
    const auto *source = m_audioSourceManager->source(sourceId);
    if (!source)
        return;
    createRemovalDialog(this, source->name, tr("移除后将停止此设备的声音采集。"), [this, sourceId] {
        return m_audioSourceManager->removeSource(sourceId);
    })->open();
}

void MainWindow::showSourceRemovalDialog(const QString &sourceId)
{
    const auto name = m_videoSourceManager->sourceName(sourceId);
    if (name.isNull())
        return;
    createRemovalDialog(this, name, tr("仅从来源列表移除，不会删除磁盘上的文件。"),
                        [this, sourceId] {
                            return m_videoSourceManager->removeSource(sourceId);
                        })
        ->open();
}

void MainWindow::showSourceFiltersDialog(const QString &sourceId)
{
    if (!m_videoSourceManager->canFilterSource(sourceId))
        return;
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->property("filterSourceId").toString() == sourceId) {
            dialog->show();
            dialog->raise();
            dialog->activateWindow();
            return;
        }
    }
    createSourceFiltersDialog(this, m_videoSourceManager, sourceId)->open();
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
    m_preview = preview;
    preview->setObjectName(QStringLiteral("previewWidget"));
    connect(preview, &PreviewWidget::renderingFailed, pages, showError, Qt::QueuedConnection);
    connect(m_videoSourceAggregator, &VideoSourceAggregator::renderSourcesChanged, preview,
            &PreviewWidget::setSources);
    connect(preview, &PreviewWidget::gpuTransferFailed, m_displaySourceController,
            &DisplaySourceController::failTransfer, Qt::QueuedConnection);
    connect(preview, &PreviewWidget::selectionRequested, m_videoSourceManager,
            &VideoSourceManager::selectSource);
    connect(preview, &PreviewWidget::transformRequested, m_videoSourceManager,
            &VideoSourceManager::setSourceTransform);
    connect(m_videoSourceManager, &VideoSourceManager::currentSourceIdChanged, preview,
            [this, preview] {
                preview->setCurrentSourceId(m_videoSourceManager->currentSourceId());
            });
    preview->setCurrentSourceId(m_videoSourceManager->currentSourceId());
    pages->addWidget(preview);
    pages->setCurrentWidget(preview);
}

void MainWindow::setupStatusBar()
{
    m_outputStatusLabel = new QLabel(tr("录制 00:00:00   |   直播 00:00:00"), this);
    m_outputStatusLabel->setObjectName(QStringLiteral("outputStatus"));

    // 临时状态消息不应遮住录制和直播计时，因此使用永久状态栏控件。
    statusBar()->addPermanentWidget(m_outputStatusLabel);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::setupVideoSourcesDock()
{
    m_videoSourceDock = new QDockWidget(tr("画面来源"), this);
    m_videoSourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_videoSourceDock->setObjectName("sourcesDock");
    m_videoSourceDock->setMinimumWidth(220);
    m_videoSourceDock->setMaximumWidth(320);

    auto *panel = new VideoSourcePanel(m_videoSourceManager, m_videoSourceDock);
    m_videoSourceDock->setWidget(panel);
    connect(panel, &VideoSourcePanel::addRequested, this, &MainWindow::showSourceAddDialog);
    connect(panel, &VideoSourcePanel::removalRequested, this, &MainWindow::showSourceRemovalDialog);
    connect(panel, &VideoSourcePanel::filtersRequested, this, &MainWindow::showSourceFiltersDialog);
}

void MainWindow::setupAudioSourcesDock()
{
    m_audioSourceDock = new QDockWidget(tr("声音来源"), this);
    m_audioSourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_audioSourceDock->setObjectName("mixerDock");
    m_audioSourceDock->setMinimumWidth(220);
    m_audioSourceDock->setMaximumWidth(320);

    auto *panel = new AudioSourcePanel(m_audioSourceManager, m_audioSourceDock);
    m_audioSourceDock->setWidget(panel);
    connect(panel, &AudioSourcePanel::addRequested, this, &MainWindow::showAudioSourceAddDialog);
    connect(panel, &AudioSourcePanel::removalRequested, this, &MainWindow::showAudioSourceRemovalDialog);
}

void MainWindow::setupControlsDock()
{
    m_controlDock = new QDockWidget(tr("控制"), this);
    m_controlDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_controlDock->setObjectName(QStringLiteral("controlsDock"));
    m_controlDock->setMinimumHeight(80);
    m_controlDock->setMaximumHeight(180);

    auto *controls = createOutputControls(m_controlDock, m_outputSettings, m_recordingController,
                                          m_streamingController);
    m_controlDock->setWidget(controls);
    connect(controls->findChild<QPushButton *>("recordButton"), &QPushButton::clicked, this,
            &MainWindow::toggleRecording);
    connect(controls->findChild<QPushButton *>("streamButton"), &QPushButton::clicked, this,
            &MainWindow::toggleStreaming);
    connect(controls->findChild<QPushButton *>("settingsButton"), &QPushButton::clicked, this,
            &MainWindow::showOutputSettingsDialog);
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
