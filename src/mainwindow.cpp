#include "mainwindow.h"
#include "sourcelistmodel.h"
#include "imagesourcecontroller.h"
#include "capture/displaycapturecontroller.h"
#include "capture/cameracapturecontroller.h"
#include "thememanager.h"
#include "preview/previewwidget.h"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QResizeEvent>
#include <QStatusBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
// Preserve Qt's title-bar glyphs and actions, but tint their pixmaps because
// native Windows styles can return black icons regardless of the palette.
class ThemedDockWidget final : public QDockWidget
{
public:
    ThemedDockWidget(const QString &title, ThemeManager &theme, QWidget *parent)
        : QDockWidget(title, parent), m_theme(theme) {}
protected:
    bool event(QEvent *event) override
    {
        const bool handled = QDockWidget::event(event);
        if (!m_refreshing && (event->type() == QEvent::StyleChange
            || event->type() == QEvent::PaletteChange || event->type() == QEvent::Show)) {
            m_refreshing = true;
            for (auto *button : findChildren<QAbstractButton *>(QString(), Qt::FindDirectChildrenOnly)) {
                const auto original = button->icon();
                if (original.isNull()) continue;
                QIcon tinted;
                for (auto mode : {QIcon::Normal, QIcon::Disabled}) {
                    auto pixmap = original.pixmap(button->iconSize(), button->devicePixelRatioF());
                    {
                        QPainter painter(&pixmap);
                        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
                        painter.fillRect(pixmap.rect(), m_theme.color(mode == QIcon::Disabled
                            ? QStringLiteral("icon.disabled") : QStringLiteral("icon.primary")));
                    }
                    tinted.addPixmap(pixmap, mode);
                }
                button->setIcon(tinted);
            }
            m_refreshing = false;
        }
        return handled;
    }
private:
    ThemeManager &m_theme;
    bool m_refreshing = false;
};
}

MainWindow::MainWindow(ThemeManager &theme, QWidget *parent)
    : QMainWindow(parent), m_theme(theme)
{
    Q_ASSERT(m_theme.isReady());
    applyTheme();
    connect(&m_theme, &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
    m_sourceListModel = new SourceListModel;
    m_imageSources = new ImageSourceController(*m_sourceListModel, this);
    m_displaySources = new DisplayCaptureController(*m_sourceListModel, this);
    m_cameraSources = new CameraCaptureController(*m_sourceListModel, this);
    connect(m_displaySources, &DisplayCaptureController::sourcesChanged,
            m_cameraSources, &CameraCaptureController::setOtherSources);
    connect(m_imageSources, &ImageSourceController::sourcesChanged,
            m_displaySources, &DisplayCaptureController::setImageSources);
    setWindowTitle(QStringLiteral("OpenLive Studio"));
    setMinimumSize(960, 640);
    resize(1280, 800);

    setupMenuBar();
    setupPreview();
    setupStatusBar();
    const auto showSourceError = [this](const QString &id, const QString &message) {
        QString name = tr("图片");
        for (int row = 0; row < m_sourceListModel->rowCount(); ++row) {
            const auto index = m_sourceListModel->index(row, 0);
            if (index.data(SourceListModel::IdRole).toString() == id) {
                name = index.data(SourceListModel::NameRole).toString();
                break;
            }
        }
        statusBar()->showMessage(tr("来源“%1”：%2").arg(name, message));
    };
    connect(m_imageSources, &ImageSourceController::sourceError, this, showSourceError);
    connect(m_displaySources, &DisplayCaptureController::sourceError, this, showSourceError);
    connect(m_cameraSources, &CameraCaptureController::sourceError, this, showSourceError);
    if (auto *preview = findChild<PreviewWidget *>(QStringLiteral("previewWidget")))
        connect(preview, &PreviewWidget::sourceError, this, showSourceError, Qt::QueuedConnection);
    setupSourcesDock();
    setupMixerDock();
    setupControlsDock();
    setupViewActions();
    setupThemeActions();
    setupHelpActions();
    resetDockLayout();
    updateStatusLayout();
    // Destroy the dock QML contexts before their shared state.
    m_sourceListModel->setParent(this);
    applyTheme();
    if (!m_theme.lastError().isEmpty())
        statusBar()->showMessage(tr("主题配置提示：%1").arg(m_theme.lastError()));
}

MainWindow::~MainWindow() = default;

void MainWindow::applyTheme()
{
    const auto color = [this](const char *key) { return m_theme.color(QLatin1String(key)); };
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
        "QMenuBar, QStatusBar { background-color: %1; color: %2; }"
        "QMenu { background-color: %3; color: %2; border: 1px solid %4; }"
        "QMenuBar::item:selected, QMenu::item:selected { background-color: %5; color: %2; }"
        "QMenuBar::item:pressed { background-color: %6; }"
        "QMenu::item:disabled { color: %7; }"
        "QMenu::separator { background-color: %4; height: 1px; }"
        "QDockWidget { color: %2; }"
        "QDockWidget::title { background-color: %1; text-align: left center; }"
        "QMainWindow::separator { background-color: %4; }"
        "QMainWindow::separator:hover { background-color: %8; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: %2; }"
        "QLabel:disabled { color: %7; }"
        "QLabel[themeError=\"true\"] { color: %9; background-color: %3; }")
        .arg(hex("background.header"), hex("text.primary"), hex("background.panel"),
             hex("border.default"), hex("background.hovered"), hex("background.selected"),
             hex("text.disabled"), hex("border.focus"), hex("status.error")));
    // Style-sheet repolishing restores cached palettes. Apply the new palette
    // afterwards, including child windows (floating docks and dialogs).
    // Keep the application's global palette untouched.
    setPalette(colors);
    for (auto *dock : {m_sourcesDock, m_mixerDock, m_controlsDock})
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
    auto *theme = &m_theme;
    view->rootContext()->setContextProperty(QStringLiteral("uiTheme"), theme);
    view->setClearColor(theme->color(QStringLiteral("background.panel")));
    connect(theme, &ThemeManager::themeChanged, view, [theme, view] {
        view->setClearColor(theme->color(QStringLiteral("background.panel")));
    });
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
            if (!m_sourceListModel->canFilterSource(id)) dialog->close();
        };
        connect(m_sourceListModel, &QAbstractItemModel::rowsRemoved, dialog, closeIfRemoved);
        connect(m_sourceListModel, &QAbstractItemModel::modelReset, dialog, closeIfRemoved);
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
    errorLabel->setProperty("themeError", true);
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
                    QVariant::fromValue(m_imageSources));
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml")
                || source.fileName() == QStringLiteral("DisplayPropertiesDialog.qml"))
                initialProperties.insert(QStringLiteral("displayCaptureController"), QVariant::fromValue(m_displaySources));
            if (source.fileName() == QStringLiteral("SourceAddDialog.qml")
                || source.fileName() == QStringLiteral("CameraPropertiesDialog.qml"))
                initialProperties.insert(QStringLiteral("cameraCaptureController"), QVariant::fromValue(m_cameraSources));
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
                connect(dialog, &QDialog::finished, m_imageSources,
                    [this, root] { m_imageSources->cancelAdd(root); });
            }
            if (root->metaObject()->indexOfSignal("closeRequested()") >= 0)
                connect(root, SIGNAL(closeRequested()), dialog, SLOT(close()));
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
    const QString initialName = m_sourceListModel->suggestedSourceName(tr("显示器采集"));
    if (initialName.isEmpty())
        return;
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml")),
        tr("添加来源"), {{QStringLiteral("sourceListModel"), QVariant::fromValue(m_sourceListModel)},
                       {QStringLiteral("initialName"), initialName}});
}

void MainWindow::showSourceRemovalDialog(const QString &sourceId)
{
    if (sourceId.isEmpty())
        return;
    auto *model = m_sourceListModel;
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(SourceListModel::IdRole).toString() != sourceId)
            continue;
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")),
            tr("移除来源"), {{QStringLiteral("sourceListModel"), QVariant::fromValue(m_sourceListModel)},
                           {QStringLiteral("sourceId"), sourceId},
                           {QStringLiteral("sourceName"), index.data(SourceListModel::NameRole)}});
        return;
    }
}

void MainWindow::showSourceFiltersDialog(const QString &sourceId)
{
    if (!m_sourceListModel->canFilterSource(sourceId)) return;
    for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dialog->property("filterSourceId").toString() == sourceId && dialog->isVisible()) {
            dialog->raise();
            dialog->activateWindow();
            return;
        }
    }
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceFiltersDialog.qml")),
        tr("来源滤镜"), {{QStringLiteral("sourceListModel"), QVariant::fromValue(m_sourceListModel)},
                         {QStringLiteral("sourceId"), sourceId}});
}

void MainWindow::showSourcePropertiesDialog(const QString &sourceId)
{
    const bool camera = !m_sourceListModel->cameraProperties(sourceId).isEmpty();
    if (!camera && m_sourceListModel->displayProperties(sourceId).isEmpty()) return;
    for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (dialog->property("displaySourceId").toString() == sourceId && dialog->isVisible()) {
            dialog->raise();
            dialog->activateWindow();
            return;
        }
    }
    if (camera) {
        m_cameraSources->refreshDevices();
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/CameraPropertiesDialog.qml")),
            tr("摄像头来源属性"), {{QStringLiteral("sourceListModel"), QVariant::fromValue(m_sourceListModel)},
                                   {QStringLiteral("sourceId"), sourceId}});
        return;
    }
    m_displaySources->refreshDisplays();
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/DisplayPropertiesDialog.qml")),
        tr("显示器来源属性"), {{QStringLiteral("sourceListModel"), QVariant::fromValue(m_sourceListModel)},
                              {QStringLiteral("sourceId"), sourceId}});
}

void MainWindow::setupMenuBar()
{
    m_viewMenu = menuBar()->addMenu(tr("视图"));
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));

    m_themeMenu = menuBar()->addMenu(tr("主题"));
    m_themeMenu->setObjectName(QStringLiteral("themeMenu"));

    m_helpMenu = menuBar()->addMenu(tr("帮助"));
    m_helpMenu->setObjectName(QStringLiteral("helpMenu"));
}

void MainWindow::setupViewActions()
{
    for (auto *dock : {m_sourcesDock, m_mixerDock, m_controlsDock}) {
        Q_ASSERT(dock);
        // The dock owns this action and keeps its checked state in sync.
        auto *action = dock->toggleViewAction();
        action->setObjectName(dock->objectName() + QStringLiteral("ViewAction"));
        m_viewMenu->addAction(action);
    }
    m_viewMenu->addSeparator();
    auto *resetAction = m_viewMenu->addAction(tr("重置布局"));
    resetAction->setObjectName(QStringLiteral("resetLayoutAction"));
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetDockLayout);
}

void MainWindow::setupHelpActions()
{
    auto *aboutAction = m_helpMenu->addAction(tr("关于 OpenLive Studio…"));
    aboutAction->setObjectName(QStringLiteral("aboutAction"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        const QUrl source(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/AboutDialog.qml"));
        for (auto *dialog : findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly)) {
            if (dialog->property("qmlSource").toUrl() == source && dialog->isVisible()) {
                dialog->raise();
                dialog->activateWindow();
                return;
            }
        }
        const QString version = QCoreApplication::applicationVersion();
        showDialog(source, tr("关于 OpenLive Studio"), {
            {QStringLiteral("productName"), windowTitle()},
            {QStringLiteral("versionText"), version.isEmpty() ? tr("开发版本（未设置版本号）") : version},
            {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())}
        });
    });
}

void MainWindow::setupThemeActions()
{
    auto *group = new QActionGroup(m_themeMenu);
    group->setExclusive(true);
    for (const auto *id : {"dark", "light"}) {
        auto *action = m_themeMenu->addAction(QLatin1String(id) == QLatin1String("dark") ? tr("深色") : tr("浅色"));
        action->setObjectName(QLatin1String(id) + QStringLiteral("ThemeAction"));
        action->setData(QString::fromLatin1(id));
        action->setCheckable(true);
        group->addAction(action);
    }
    const auto syncSelection = [this, group] {
        for (auto *action : group->actions())
            action->setChecked(action->data().toString() == m_theme.themeId());
    };
    syncSelection();
    connect(&m_theme, &ThemeManager::themeChanged, group, syncSelection);
    connect(group, &QActionGroup::triggered, this, [this, syncSelection](QAction *action) {
        if (m_theme.setTheme(action->data().toString()))
            statusBar()->clearMessage();
        else
            statusBar()->showMessage(tr("无法切换主题：%1").arg(m_theme.lastError()));
        // QAction checks itself before triggered(); failed saves emit no themeChanged.
        syncSelection();
    });
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
    errorLabel->setProperty("themeError", true);
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
    connect(m_cameraSources, &CameraCaptureController::sourcesChanged, preview, &PreviewWidget::setSources);
    connect(preview, &PreviewWidget::gpuTransferFailed, m_displaySources, &DisplayCaptureController::failTransfer, Qt::QueuedConnection);
    connect(preview, &PreviewWidget::selectionRequested, m_sourceListModel, &SourceListModel::selectSource);
    connect(preview, &PreviewWidget::transformRequested, m_sourceListModel, &SourceListModel::setSourceTransform);
    connect(m_sourceListModel, &SourceListModel::currentSourceIdChanged, preview, [this, preview] {
        preview->setSelectedSourceId(m_sourceListModel->currentSourceId());
    });
    preview->setSelectedSourceId(m_sourceListModel->currentSourceId());
    pages->addWidget(preview);
    pages->setCurrentWidget(preview);
}

void MainWindow::setupStatusBar()
{
    m_outputStatus = new QLabel(
        tr("REC 未开始 · 时长 —   |   LIVE 未开始 · 时长 —"), this);
    m_outputStatus->setObjectName(QStringLiteral("outputStatus"));

    m_outputMetrics = new QLabel(
        tr("输出帧率 —   |   推流丢帧 —   |   发送码率 —   |   CPU —   |   内存 —"), this);
    m_outputMetrics->setObjectName(QStringLiteral("outputMetrics"));

    // Output states remain visible even when a temporary status message appears.
    statusBar()->addPermanentWidget(m_outputStatus);
    statusBar()->addPermanentWidget(m_outputMetrics);
    statusBar()->showMessage(tr("就绪"));
}

void MainWindow::setupSourcesDock()
{
    m_sourcesDock = new ThemedDockWidget(tr("来源"), m_theme, this);
    m_sourcesDock->setObjectName(QStringLiteral("sourcesDock"));
    m_sourcesDock->setMinimumWidth(220);

    auto *pages = new QStackedWidget(m_sourcesDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("sourcesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("sourceListModel"), m_sourceListModel);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("来源面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("sourcesLoadError"));
    errorLabel->setProperty("themeError", true);
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

    m_sourcesDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
}

void MainWindow::setupMixerDock()
{
    m_mixerDock = new ThemedDockWidget(tr("混音器"), m_theme, this);
    m_mixerDock->setObjectName(QStringLiteral("mixerDock"));
    m_mixerDock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(m_mixerDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("mixerQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("混音器面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("mixerLoadError"));
    errorLabel->setProperty("themeError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("混音器面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("混音器面板加载失败，请查看日志。"));
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
                statusBar()->showMessage(tr("混音器面板已加载"), 3000);
            pages->setCurrentWidget(view);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    m_mixerDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml")));
}

void MainWindow::setupControlsDock()
{
    m_controlsDock = new ThemedDockWidget(tr("控制"), m_theme, this);
    m_controlsDock->setObjectName(QStringLiteral("controlsDock"));
    m_controlsDock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(m_controlsDock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("controlsQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setMinimumHeight(124);
    configureQuickWidget(view);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("控制面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("controlsLoadError"));
    errorLabel->setProperty("themeError", true);
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
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    m_controlsDock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ControlsPanel.qml")));
}

void MainWindow::resetDockLayout()
{
    const QList<QDockWidget *> docks{m_sourcesDock,
                                     m_mixerDock, m_controlsDock};

    const bool wasUpdating = updatesEnabled();
    setUpdatesEnabled(false);
    // Remove old splits and tab groups while preserving each dock and QML root.
    for (auto *dock : docks) {
        Q_ASSERT(dock);
        dock->setFloating(false);
        removeDockWidget(dock);
    }
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_sourcesDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_mixerDock);
    addDockWidget(Qt::BottomDockWidgetArea, m_controlsDock);
    splitDockWidget(m_mixerDock, m_controlsDock, Qt::Horizontal);
    for (auto *dock : docks)
        dock->show();
    resizeDocks({m_sourcesDock}, {238}, Qt::Horizontal);
    resizeDocks({m_mixerDock, m_controlsDock},
                {qMax(200, width() - 238 - 286 - 12), 286}, Qt::Horizontal);
    resizeDocks({m_mixerDock}, {198}, Qt::Vertical);
    setUpdatesEnabled(wasUpdating);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateStatusLayout();
}

void MainWindow::updateStatusLayout()
{
    if (!m_outputStatus || !m_outputMetrics)
        return;

    // Reserve space for the status message, spacing and the window size grip.
    const int requiredWidth = m_outputStatus->sizeHint().width()
        + m_outputMetrics->sizeHint().width() + 180;
    m_outputMetrics->setVisible(width() >= requiredWidth);
}
