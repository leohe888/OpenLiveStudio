#include "ui/mainwindow.h"
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
#include <QFileDialog>
#include <QPointer>
#include <QtAlgorithms>

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QStatusBar>
#include <QShortcut>
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
    // the dock QML views are destroyed before the models/settings they reference.
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
    const QUrl url(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/OutputSettingsDialog.qml"));
    for (auto *dialog:findChildren<QDialog *>()) if (dialog->property("qmlSource").toUrl()==url) {
        dialog->show(); dialog->raise(); dialog->activateWindow(); return;
    }
    showDialog(url,tr("设置"),{{"outputSettings",QVariant::fromValue(m_outputSettings)}});
}

void MainWindow::browseRecordingDirectory()
{
    QPointer<QObject> root=sender(); if (!root) return;
    const auto path=QFileDialog::getExistingDirectory(this,tr("选择录制保存目录"),root->property("directory").toString());
    if (root && !path.isEmpty()) root->setProperty("directory",path);
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

void MainWindow::configureQuickWidget(QQuickWidget *view)
{
    view->rootContext()->setContextProperty(QStringLiteral("uiColors"), UiColors::values());
    view->setClearColor(UiColors::color(QStringLiteral("background.panel")));
}

void MainWindow::showDialog(const QUrl &source, const QString &title,
                            const QVariantMap &properties)
{
    auto *dialog = new QDialog(this);
    dialog->setPalette(palette());
    dialog->setObjectName(QStringLiteral("qmlDialog"));
    dialog->setProperty("qmlSource", source);
    if (source.fileName() == QStringLiteral("SourceFiltersDialog.qml")
        || source.fileName() == QStringLiteral("DisplayPropertiesDialog.qml")
        || source.fileName() == QStringLiteral("CameraPropertiesDialog.qml")) {
        const QString id = properties.value(QStringLiteral("sourceId")).toString();
        dialog->setProperty(source.fileName() == QStringLiteral("SourceFiltersDialog.qml")
            ? "filterSourceId" : "displaySourceId", id);
        const auto closeIfRemoved = [this, dialog, id] {
            if (!m_videoSourceModel->canFilterSource(id)) dialog->close();
        };
        connect(m_videoSourceModel, &QAbstractItemModel::rowsRemoved, dialog, closeIfRemoved);
        connect(m_videoSourceModel, &QAbstractItemModel::modelReset, dialog, closeIfRemoved);
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->resize(420, 240);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *pages = new QStackedWidget(dialog);
    layout->addWidget(pages);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("dialogQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    pages->addWidget(view);
    auto *errorLabel = new QLabel(tr("对话框加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("dialogLoadError"));
    errorLabel->setProperty("uiError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);
    const auto showError = [pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("对话框加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
    };
    connect(view, &QQuickWidget::sceneGraphError, dialog,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });
    auto *component = new QQmlComponent(view->engine(), view);
    connect(component, &QQmlComponent::statusChanged, dialog,
            [this, dialog, view, component, source, properties, showError](QQmlComponent::Status status) {
        if (status == QQmlComponent::Error) {
            QStringList errors;
            for (const auto &error : component->errors())
                errors.append(error.toString());
            showError(errors.join('\n'));
        } else if (status == QQmlComponent::Ready) {
            // Compatible with Qt 6.5; properties precede Component.onCompleted.
            QVariantMap initialProperties = properties;
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml"))
                initialProperties.insert(QStringLiteral("imageSourceController"),
                    QVariant::fromValue(m_imageSourceController));
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml")
                || source.fileName() == QStringLiteral("DisplayPropertiesDialog.qml"))
                initialProperties.insert(QStringLiteral("displaySourceController"), QVariant::fromValue(m_displaySourceController));
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml")
                || source.fileName() == QStringLiteral("CameraPropertiesDialog.qml"))
                initialProperties.insert(QStringLiteral("cameraSourceController"), QVariant::fromValue(m_cameraSourceController));
            auto *object = component->createWithInitialProperties(initialProperties, view->rootContext());
            auto *root = qobject_cast<QQuickItem *>(object);
            if (!root) {
                QStringList errors;
                for (const auto &error : component->errors())
                    errors.append(error.toString());
                showError(errors.isEmpty() ? tr("QML 根对象必须是可视 Item。") : errors.join('\n'));
                delete object;
                return;
            }
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml")) {
                auto *cancel = new QShortcut(QKeySequence(Qt::Key_Escape), dialog);
                connect(cancel, &QShortcut::activated, dialog, &QDialog::reject);
                connect(dialog, &QDialog::finished, m_imageSourceController,
                    [this, root] { m_imageSourceController->cancelAdd(root); });
            }
            if (root->metaObject()->indexOfSignal("closeRequested()") >= 0)
                connect(root, SIGNAL(closeRequested()), dialog, SLOT(close()));
            if (root->metaObject()->indexOfSignal("browseDirectoryRequested()") >= 0)
                connect(root,SIGNAL(browseDirectoryRequested()),this,SLOT(browseRecordingDirectory()));
            const QSize contentSize(qMax(1, qRound(root->implicitWidth())),
                                    qMax(1, qRound(root->implicitHeight())));
            view->setContent(source, component, root);
            if (contentSize.width() > 1 && contentSize.height() > 1)
                dialog->resize(contentSize);
        }
    });
    component->loadUrl(source, QQmlComponent::PreferSynchronous);
    dialog->open();
}

void MainWindow::showSourceAddDialog()
{
    const QString initialName = m_videoSourceModel->suggestedSourceName(tr("显示器采集"));
    if (initialName.isEmpty())
        return;
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml")),
        tr("添加来源"), {{QStringLiteral("videoSourceModel"), QVariant::fromValue(m_videoSourceModel)},
                       {QStringLiteral("initialName"), initialName}});
}

void MainWindow::showAudioSourceAddDialog()
{
    showAudioSourcePropertiesDialog({});
}

void MainWindow::showAudioSourcePropertiesDialog(const QString &sourceId)
{
    if (!sourceId.isEmpty() && m_audioSourceModel->source(sourceId).isEmpty()) return;
    const QUrl url(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/AudioSourceDialog.qml"));
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->property("qmlSource").toUrl() == url && dialog->property("audioSourceId").toString() == sourceId) {
            dialog->show(); dialog->raise(); dialog->activateWindow(); return;
        }
    }
    showDialog(url, sourceId.isEmpty() ? tr("添加声音来源") : tr("声音来源属性"),
        {{"audioSources", QVariant::fromValue(m_audioSourceModel)}, {"sourceId", sourceId}});
    for (auto *dialog : findChildren<QDialog *>()) {
        if (dialog->property("qmlSource").toUrl() != url || dialog->property("audioBound").toBool()) continue;
        dialog->setProperty("audioBound", true);
        dialog->setProperty("audioSourceId", sourceId);
        if (!sourceId.isEmpty()) connect(m_audioSourceModel, &QAbstractItemModel::rowsRemoved, dialog, [this, dialog, sourceId] {
            if (m_audioSourceModel->source(sourceId).isEmpty()) dialog->close();
        });
    }
}

void MainWindow::showAudioSourceRemovalDialog(const QString &sourceId)
{
    const auto source = m_audioSourceModel->source(sourceId);
    if (source.isEmpty()) return;
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")), tr("移除声音来源"),
        {{"videoSourceModel", QVariant::fromValue(m_audioSourceModel)}, {"sourceId", sourceId}, {"sourceName", source.value("name")},
         {"removalHint", tr("移除后将停止此设备的声音采集。")}});
}

void MainWindow::showSourceRemovalDialog(const QString &sourceId)
{
    if (sourceId.isEmpty())
        return;
    auto *model = m_videoSourceModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(VideoSourceModel::IdRole).toString() != sourceId)
            continue;
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")),
            tr("移除来源"), {{QStringLiteral("videoSourceModel"), QVariant::fromValue(m_videoSourceModel)},
                           {QStringLiteral("sourceId"), sourceId},
                           {QStringLiteral("sourceName"), index.data(VideoSourceModel::NameRole)}});
        return;
    }
}

void MainWindow::showSourceFiltersDialog(const QString &sourceId)
{
    if (!m_videoSourceModel->canFilterSource(sourceId)) return;
    for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dialog->property("filterSourceId").toString() == sourceId && dialog->isVisible()) {
            dialog->raise();
            dialog->activateWindow();
            return;
        }
    }
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceFiltersDialog.qml")),
        tr("来源滤镜"), {{QStringLiteral("videoSourceModel"), QVariant::fromValue(m_videoSourceModel)},
                         {QStringLiteral("sourceId"), sourceId}});
}

void MainWindow::showSourcePropertiesDialog(const QString &sourceId)
{
    const bool camera = !m_videoSourceModel->cameraProperties(sourceId).isEmpty();
    if (!camera && m_videoSourceModel->displayProperties(sourceId).isEmpty()) return;
    for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dialog->property("displaySourceId").toString() == sourceId && dialog->isVisible()) {
            dialog->raise();
            dialog->activateWindow();
            return;
        }
    }
    if (camera) {
        m_cameraSourceController->refreshDevices();
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/CameraPropertiesDialog.qml")),
            tr("摄像头来源属性"), {{QStringLiteral("videoSourceModel"), QVariant::fromValue(m_videoSourceModel)},
                                   {QStringLiteral("sourceId"), sourceId}});
        return;
    }
    m_displaySourceController->refreshDisplays();
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/DisplayPropertiesDialog.qml")),
        tr("显示器来源属性"), {{QStringLiteral("videoSourceModel"), QVariant::fromValue(m_videoSourceModel)},
                              {QStringLiteral("sourceId"), sourceId}});
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
    m_videoSourceDock->setObjectName(QStringLiteral("sourcesDock"));
    m_videoSourceDock->setMinimumWidth(220);
    m_videoSourceDock->setMaximumWidth(320);

    auto *pages = new QStackedWidget(m_videoSourceDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("sourcesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("videoSourceModel"), m_videoSourceModel);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("来源面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("sourcesLoadError"));
    errorLabel->setProperty("uiError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("来源面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("来源面板加载失败，请查看日志。"));
    };
    connect(view, &QQuickWidget::statusChanged, this,
            [this, view, pages, errorLabel, showError](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            QStringList errors;
            for (const auto &error : view->errors())
                errors.append(error.toString());
            showError(errors.join('\n'));
        } else if (status == QQuickWidget::Ready) {
            if (pages->currentWidget() == errorLabel)
                statusBar()->showMessage(tr("来源面板已加载"), 3000);
            pages->setCurrentWidget(view);
            connect(view->rootObject(), SIGNAL(sourceAddDialogRequested()),
                    this, SLOT(showSourceAddDialog()), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(sourceRemovalDialogRequested(QString)),
                    this, SLOT(showSourceRemovalDialog(QString)), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(sourceFiltersDialogRequested(QString)),
                    this, SLOT(showSourceFiltersDialog(QString)), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(sourcePropertiesDialogRequested(QString)),
                    this, SLOT(showSourcePropertiesDialog(QString)), Qt::UniqueConnection);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    m_videoSourceDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
}

void MainWindow::setupMixerDock()
{
    m_audioSourceDock = new QDockWidget(tr("声音来源"), this);
    m_audioSourceDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_audioSourceDock->setObjectName(QStringLiteral("mixerDock"));
    m_audioSourceDock->setMinimumWidth(220);
    m_audioSourceDock->setMaximumWidth(320);

    auto *pages = new QStackedWidget(m_audioSourceDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("mixerQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    view->rootContext()->setContextProperty(QStringLiteral("audioSourceModel"), m_audioSourceModel);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("声音来源面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("mixerLoadError"));
    errorLabel->setProperty("uiError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("声音来源面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("声音来源面板加载失败，请查看日志。"));
    };
    connect(view, &QQuickWidget::statusChanged, this,
            [this, view, pages, errorLabel, showError](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            QStringList errors;
            for (const auto &error : view->errors())
                errors.append(error.toString());
            showError(errors.join('\n'));
        } else if (status == QQuickWidget::Ready) {
            if (pages->currentWidget() == errorLabel)
                statusBar()->showMessage(tr("声音来源面板已加载"), 3000);
            pages->setCurrentWidget(view);
            connect(view->rootObject(), SIGNAL(soundSourceAddDialogRequested()), this, SLOT(showAudioSourceAddDialog()), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(soundSourcePropertiesDialogRequested(QString)), this, SLOT(showAudioSourcePropertiesDialog(QString)), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(soundSourceRemovalDialogRequested(QString)), this, SLOT(showAudioSourceRemovalDialog(QString)), Qt::UniqueConnection);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    m_audioSourceDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml")));
}

void MainWindow::setupControlsDock()
{
    m_controlDock = new QDockWidget(tr("控制"), this);
    m_controlDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_controlDock->setObjectName(QStringLiteral("controlsDock"));
    m_controlDock->setMinimumHeight(80);
    m_controlDock->setMaximumHeight(180);

    auto *pages = new QStackedWidget(m_controlDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("controlsQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    view->rootContext()->setContextProperty(QStringLiteral("outputSettings"),m_outputSettings);
    view->rootContext()->setContextProperty(QStringLiteral("recordingController"),m_recordingController);
    view->rootContext()->setContextProperty(QStringLiteral("streamingController"),m_streamingController);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("控制面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("controlsLoadError"));
    errorLabel->setProperty("uiError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("控制面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("控制面板加载失败，请查看日志。"));
    };
    connect(view, &QQuickWidget::statusChanged, this,
            [this, view, pages, errorLabel, showError](QQuickWidget::Status status) {
        if (status == QQuickWidget::Error) {
            QStringList errors;
            for (const auto &error : view->errors())
                errors.append(error.toString());
            showError(errors.join('\n'));
        } else if (status == QQuickWidget::Ready) {
            if (pages->currentWidget() == errorLabel)
                statusBar()->showMessage(tr("控制面板已加载"), 3000);
            pages->setCurrentWidget(view);
            connect(view->rootObject(),SIGNAL(settingsDialogRequested()),this,SLOT(showOutputSettingsDialog()),Qt::UniqueConnection);
            connect(view->rootObject(),SIGNAL(recordingToggleRequested()),this,SLOT(toggleRecording()),Qt::UniqueConnection);
            connect(view->rootObject(),SIGNAL(streamingToggleRequested()),this,SLOT(toggleStreaming()),Qt::UniqueConnection);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    m_controlDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ControlsPanel.qml")));
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
