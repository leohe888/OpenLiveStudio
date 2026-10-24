#include "mainwindow.h"
#include "studiostate.h"
#include "scenesnapshotbuilder.h"
#include "compositionsession.h"
#include "previewwidget.h"
#include "thememanager.h"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
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
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QShowEvent>
#include <QTimer>
#include <memory>

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

// QOpenGLWidget can fail before initializeGL(), so observe its first visible paint.
class MainPreview final : public PreviewWidget
{
public:
    using PreviewWidget::PreviewWidget;
    std::function<void(const QString &)> initializationFailed;
protected:
    void showEvent(QShowEvent *event) override
    {
        PreviewWidget::showEvent(event);
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && !isReady() && initializationFailed)
                initializationFailed(tr("无法创建 OpenGL 3.3 预览上下文。"));
        });
    }
};
}

MainWindow::MainWindow(ThemeManager &theme, QWidget *parent)
    : QMainWindow(parent), m_theme(theme)
{
    Q_ASSERT(m_theme.isReady());
    applyTheme();
    connect(&m_theme, &ThemeManager::themeChanged, this, &MainWindow::applyTheme);
    m_studioState = new StudioState;
    setWindowTitle(QStringLiteral("OpenLive Studio"));
    setMinimumSize(960, 640);
    resize(1280, 800);

    setupMenuBar();
    setupPreview();
    setupStatusBar();
    setupScenesDock();
    setupSourcesDock();
    setupMixerDock();
    setupControlsDock();
    setupViewActions();
    setupHelpActions();
    resetDockLayout();
    updateStatusLayout();
    // Destroy the dock QML contexts before their shared state.
    m_studioState->setParent(this);
    applyTheme();
    if (!m_theme.lastError().isEmpty())
        statusBar()->showMessage(tr("主题配置提示：%1").arg(m_theme.lastError()));
}

MainWindow::~MainWindow()
{
    // Stop decoding before tearing down the view; it returns leases before the
    // synchronous context-release handler joins the composition worker.
    delete findChild<SceneSnapshotBuilder *>();
    delete takeCentralWidget();
    delete findChild<CompositionSession *>();
}

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
        "QDockWidget::title { background-color: %1; }"
        "QMainWindow::separator { background-color: %4; }"
        "QMainWindow::separator:hover { background-color: %8; }"
        "QStatusBar::item { border: none; }"
        "QLabel { color: %2; }"
        "QLabel:disabled { color: %7; }"
        "QLabel[themeError=\"true\"] { color: %9; background-color: %3; }"
        "QLabel#previewUnavailable { border: 1px solid %4; }")
        .arg(hex("background.header"), hex("text.primary"), hex("background.panel"),
             hex("border.default"), hex("background.hovered"), hex("background.selected"),
             hex("text.disabled"), hex("border.focus"), hex("status.error")));
    // Style-sheet repolishing restores cached palettes. Apply the new palette
    // afterwards, including child windows (floating docks and dialogs).
    // Keep the application's global palette untouched.
    setPalette(colors);
    for (auto *dock : findChildren<QDockWidget *>()) dock->setPalette(colors);
    for (auto *dialog : findChildren<QDialog *>()) dialog->setPalette(colors);
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
            [dialog, view, component, source, properties, showError](QQmlComponent::Status status) {
        if (status == QQmlComponent::Error) {
            QStringList errors;
            for (const auto &error : component->errors())
                errors.append(error.toString());
            showError(errors.join('\n'));
        } else if (status == QQmlComponent::Ready) {
            // Compatible with Qt 6.5; properties precede Component.onCompleted.
            auto *object = component->createWithInitialProperties(properties, view->rootContext());
            auto *root = qobject_cast<QQuickItem *>(object);
            if (!root) {
                QStringList errors;
                for (const auto &error : component->errors())
                    errors.append(error.toString());
                showError(errors.isEmpty() ? tr("QML 根对象必须是可视 Item。") : errors.join('\n'));
                delete object;
                return;
            }
            if (root->metaObject()->indexOfSignal("closeRequested()") >= 0)
                connect(root, SIGNAL(closeRequested()), dialog, SLOT(close()));
            const QSize contentSize(qMax(1, qRound(root->implicitWidth())),
                                    qMax(1, qRound(root->implicitHeight())));
            view->setContent(source, component, root);
            if (root->metaObject()->indexOfSignal("browseImageFileRequested()") >= 0) {
                auto *picker = new QFileDialog(dialog, tr("选择图像文件"));
                picker->setObjectName(QStringLiteral("imageFileDialog"));
                picker->setFileMode(QFileDialog::ExistingFile);
                picker->setNameFilters({tr("图像文件 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.svg)"),
                                        tr("所有文件 (*)")});
                connect(root, SIGNAL(browseImageFileRequested()), picker, SLOT(open()));
                connect(picker, &QFileDialog::fileSelected, root, [root](const QString &path) {
                    root->setProperty("filePath", path);
                    root->setProperty("errorText", QString());
                });
            }
            if (contentSize.width() > 1 && contentSize.height() > 1)
                dialog->resize(contentSize);
        }
    });
    component->loadUrl(source, QQmlComponent::PreferSynchronous);
    dialog->open();
}

void MainWindow::showSceneNameDialog(const QString &sceneId, const QString &initialName)
{
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SceneNameDialog.qml")),
        sceneId.isEmpty() ? tr("添加场景") : tr("重命名场景"), {
            {QStringLiteral("state"), QVariant::fromValue(m_studioState)},
            {QStringLiteral("initialName"), sceneId.isEmpty() ? m_studioState->suggestedSceneName() : initialName},
            {QStringLiteral("sceneId"), sceneId}
        });
}

void MainWindow::showSceneRemovalDialog(const QString &sceneId)
{
    auto *model = m_studioState->sceneModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(StudioState::IdRole).toString() != sceneId)
            continue;
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SceneRemovalDialog.qml")),
            tr("删除场景"), {{QStringLiteral("state"), QVariant::fromValue(m_studioState)},
            {QStringLiteral("sceneId"), sceneId},
            {QStringLiteral("sceneName"), index.data(StudioState::NameRole)}});
        return;
    }
}

void MainWindow::showSourceAddDialog(const QString &sceneId)
{
    const QString initialName = m_studioState->suggestedSourceName(sceneId, tr("图像"));
    if (initialName.isEmpty())
        return;
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml")),
        tr("添加来源"), {{QStringLiteral("state"), QVariant::fromValue(m_studioState)},
                       {QStringLiteral("sceneId"), sceneId},
                       {QStringLiteral("initialName"), initialName}});
}

void MainWindow::showSourceRemovalDialog(const QString &sceneId, const QString &sourceId)
{
    if (sceneId != m_studioState->currentSceneId() || sourceId.isEmpty())
        return;
    auto *model = m_studioState->sourceModel();
    for (int row = 0; row < model->rowCount(); ++row) {
        const auto index = model->index(row, 0);
        if (index.data(StudioState::IdRole).toString() != sourceId)
            continue;
        showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceRemovalDialog.qml")),
            tr("移除来源"), {{QStringLiteral("state"), QVariant::fromValue(m_studioState)},
                           {QStringLiteral("sceneId"), sceneId},
                           {QStringLiteral("sourceId"), sourceId},
                           {QStringLiteral("sourceName"), index.data(StudioState::NameRole)}});
        return;
    }
}

void MainWindow::setupMenuBar()
{
    m_viewMenu = menuBar()->addMenu(tr("视图"));
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));

    m_helpMenu = menuBar()->addMenu(tr("帮助"));
}

void MainWindow::setupViewActions()
{
    for (const auto *name : {"scenesDock", "sourcesDock", "mixerDock",
                             "controlsDock"}) {
        auto *dock = findChild<QDockWidget *>(QLatin1String(name));
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

void MainWindow::resetDockLayout()
{
    auto *scenes = findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *sources = findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
    auto *mixer = findChild<QDockWidget *>(QStringLiteral("mixerDock"));
    auto *controls = findChild<QDockWidget *>(QStringLiteral("controlsDock"));
    const QList<QDockWidget *> docks{scenes, sources, mixer, controls};

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
    addDockWidget(Qt::LeftDockWidgetArea, scenes);
    addDockWidget(Qt::LeftDockWidgetArea, sources);
    splitDockWidget(scenes, sources, Qt::Vertical);
    addDockWidget(Qt::BottomDockWidgetArea, mixer);
    addDockWidget(Qt::BottomDockWidgetArea, controls);
    splitDockWidget(mixer, controls, Qt::Horizontal);
    for (auto *dock : docks)
        dock->show();
    resizeDocks({scenes}, {238}, Qt::Horizontal);
    resizeDocks({scenes, sources}, {220, 440}, Qt::Vertical);
    resizeDocks({mixer, controls},
                {qMax(200, width() - 238 - 286 - 12), 286}, Qt::Horizontal);
    resizeDocks({mixer}, {198}, Qt::Vertical);
    setUpdatesEnabled(wasUpdating);
}

void MainWindow::setupPreview()
{
    auto *container = new QWidget(this);
    container->setObjectName(QStringLiteral("previewContainer"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);

    auto *pages = new QStackedWidget(container);
    layout->addWidget(pages, 1);
    auto *failure = new QLabel(pages);
    failure->setObjectName(QStringLiteral("previewUnavailable"));
    failure->setTextFormat(Qt::PlainText);
    failure->setAlignment(Qt::AlignCenter);
    failure->setWordWrap(true);
    failure->setProperty("themeError", true);
    pages->addWidget(failure);
    setCentralWidget(container);
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")
        || QQuickWindow::sceneGraphBackend() == QStringLiteral("software")) {
        failure->setText(tr("当前软件或离屏环境不支持 OpenGL 预览。"));
        return;
    }

    auto *status = new QLabel(tr("正在加载画面…"), container);
    status->setObjectName(QStringLiteral("previewStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *preview = new MainPreview(pages);
    preview->setObjectName(QStringLiteral("previewWidget"));
    pages->addWidget(preview);
    pages->setCurrentWidget(preview);
    auto *session = new CompositionSession(this);
    auto *builder = new SceneSnapshotBuilder(m_studioState, this);
    struct Presentation {
        SceneSnapshotBuilder::Snapshot latest;
        bool hasSnapshot = false;
        QStringList errors;
    };
    auto state = std::make_shared<Presentation>();
    const auto showFailure = [pages, failure](const QString &message) {
        failure->setText(tr("预览不可用：%1").arg(message));
        pages->setCurrentWidget(failure);
    };
    preview->initializationFailed = showFailure;
    connect(preview, &PreviewWidget::graphicsError, container, showFailure);
    connect(session, &CompositionSession::errorOccurred, container, showFailure);
    const auto submit = [session, state, showFailure] {
        if (!state->hasSnapshot || !session->isRunning())
            return;
        QString error;
        if (!session->submit(state->latest.layers, state->latest.canvas, &error))
            showFailure(error);
    };
    connect(session, &CompositionSession::started, container, submit);
    connect(preview, &PreviewWidget::contextReady, container,
            [session, showFailure](QOpenGLContext *context) {
        QString error;
        if (!session->start(context, &error))
            showFailure(error);
    });
    // This connection must survive deletion of the central container.
    connect(preview, &PreviewWidget::contextAboutToBeReleased, session, [session] {
        QString error;
        if (!session->stop(&error))
            qFatal("Preview composition shutdown failed: %s", qPrintable(error));
    });
    QString error;
    if (!preview->setSession(session, &error))
        showFailure(error);
    connect(builder, &SceneSnapshotBuilder::loadingChanged, container, [builder, state, status] {
        if (builder->isLoading()) {
            state->errors.clear();
            status->setText(tr("正在加载画面…"));
        }
    });
    connect(builder, &SceneSnapshotBuilder::sourceError, container,
            [state](const SceneSnapshotBuilder::SourceError &error) {
        state->errors.append(tr("%1：%2").arg(error.name, error.message));
    });
    connect(builder, &SceneSnapshotBuilder::snapshotReady, container,
            [state, status, submit](const SceneSnapshotBuilder::Snapshot &snapshot) {
        state->latest = snapshot;
        state->hasSnapshot = true;
        status->setText(state->errors.isEmpty()
            ? (snapshot.layers.isEmpty() ? tr("当前场景没有可见图像来源") : QString())
            : state->errors.join('\n'));
        submit();
    });
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

void MainWindow::setupScenesDock()
{
    auto *dock = new ThemedDockWidget(tr("场景"), m_theme, this);
    dock->setObjectName(QStringLiteral("scenesDock"));
    dock->setMinimumWidth(180);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("scenesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("studioState"), m_studioState);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    configureQuickWidget(view);
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("场景面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("scenesLoadError"));
    errorLabel->setProperty("themeError", true);
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("场景面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("场景面板加载失败，请查看日志。"));
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
                statusBar()->showMessage(tr("场景面板已加载"), 3000);
            connect(view->rootObject(), SIGNAL(sceneNameDialogRequested(QString,QString)),
                    this, SLOT(showSceneNameDialog(QString,QString)), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(sceneRemovalDialogRequested(QString)),
                    this, SLOT(showSceneRemovalDialog(QString)), Qt::UniqueConnection);
            pages->setCurrentWidget(view);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    dock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ScenesPanel.qml")));
}

void MainWindow::setupSourcesDock()
{
    auto *dock = new ThemedDockWidget(tr("来源"), m_theme, this);
    dock->setObjectName(QStringLiteral("sourcesDock"));
    dock->setMinimumWidth(220);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("sourcesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("studioState"), m_studioState);
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
            connect(view->rootObject(), SIGNAL(sourceAddDialogRequested(QString)),
                    this, SLOT(showSourceAddDialog(QString)), Qt::UniqueConnection);
            connect(view->rootObject(), SIGNAL(sourceRemovalDialogRequested(QString,QString)),
                    this, SLOT(showSourceRemovalDialog(QString,QString)), Qt::UniqueConnection);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    dock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourcesPanel.qml")));
}

void MainWindow::setupMixerDock()
{
    auto *dock = new ThemedDockWidget(tr("混音器"), m_theme, this);
    dock->setObjectName(QStringLiteral("mixerDock"));
    dock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(dock);
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

    dock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/MixerPanel.qml")));
}

void MainWindow::setupControlsDock()
{
    auto *dock = new ThemedDockWidget(tr("控制"), m_theme, this);
    dock->setObjectName(QStringLiteral("controlsDock"));
    dock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(dock);
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

    dock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/ControlsPanel.qml")));
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
