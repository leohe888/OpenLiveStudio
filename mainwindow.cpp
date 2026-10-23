#include "mainwindow.h"
#include "studiostate.h"

#include <QAction>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlError>
#include <QQuickItem>
#include <QQuickWidget>
#include <QResizeEvent>
#include <QStatusBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_studioState = new StudioState;
    setWindowTitle(QStringLiteral("OpenLive Studio"));
    setMinimumSize(960, 640);
    resize(1280, 800);

    setupMenusAndToolBar();
    setupPreview();
    setupStatusBar();
    setupScenesDock();
    setupSourcesDock();
    setupMixerDock();
    setupTransitionsDock();
    setupControlsDock();
    setupDockViewActions();
    resetDockLayout();
    updateStatusLayout();
    // Destroy the dock QML contexts before their shared state.
    m_studioState->setParent(this);
}

MainWindow::~MainWindow() = default;

void MainWindow::showDialog(const QUrl &source, const QString &title,
                            const QVariantMap &properties)
{
    auto *dialog = new QDialog(this);
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
    dialog->winId(); // Native dialogs need the host window, not QQuickWidget's offscreen window.
    view->rootContext()->setContextProperty(QStringLiteral("dialogWindow"), dialog->windowHandle());
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(QColor(QStringLiteral("#1d1d1d")));
    pages->addWidget(view);
    auto *errorLabel = new QLabel(tr("对话框加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("dialogLoadError"));
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
    if (sceneId.isEmpty())
        return;
    showDialog(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/SourceAddDialog.qml")),
        tr("添加来源"), {{QStringLiteral("state"), QVariant::fromValue(m_studioState)},
                       {QStringLiteral("sceneId"), sceneId}});
}

void MainWindow::setupMenusAndToolBar()
{
    m_viewMenu = menuBar()->addMenu(tr("视图"));
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));

    auto *helpMenu = menuBar()->addMenu(tr("帮助"));
    auto *aboutAction = helpMenu->addAction(tr("关于 OpenLive Studio…"));
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

    auto *toolBar = addToolBar(tr("主工具栏"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setMinimumHeight(24);
}

void MainWindow::setupDockViewActions()
{
    for (const auto *name : {"scenesDock", "sourcesDock", "mixerDock",
                             "transitionsDock", "controlsDock"}) {
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

void MainWindow::resetDockLayout()
{
    auto *scenes = findChild<QDockWidget *>(QStringLiteral("scenesDock"));
    auto *sources = findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
    auto *mixer = findChild<QDockWidget *>(QStringLiteral("mixerDock"));
    auto *transitions = findChild<QDockWidget *>(QStringLiteral("transitionsDock"));
    auto *controls = findChild<QDockWidget *>(QStringLiteral("controlsDock"));
    const QList<QDockWidget *> docks{scenes, sources, mixer, transitions, controls};

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
    addDockWidget(Qt::BottomDockWidgetArea, transitions);
    splitDockWidget(mixer, transitions, Qt::Horizontal);
    addDockWidget(Qt::BottomDockWidgetArea, controls);
    splitDockWidget(transitions, controls, Qt::Horizontal);
    for (auto *dock : docks)
        dock->show();
    resizeDocks({scenes}, {238}, Qt::Horizontal);
    resizeDocks({scenes, sources}, {220, 440}, Qt::Vertical);
    resizeDocks({mixer, transitions, controls},
                {qMax(200, width() - 238 - 224 - 286 - 12), 224, 286}, Qt::Horizontal);
    resizeDocks({mixer}, {198}, Qt::Vertical);
    setUpdatesEnabled(wasUpdating);
}

void MainWindow::setupPreview()
{
    auto *container = new QWidget(this);
    container->setObjectName(QStringLiteral("previewContainer"));
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(12, 12, 12, 12);

    auto *placeholder = new QLabel(tr("尚未添加画面来源"), container);
    placeholder->setObjectName(QStringLiteral("previewPlaceholder"));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    placeholder->setStyleSheet(QStringLiteral(
        "background: #000000; color: #8c8c8c; border: 1px solid #2e2e2e;"));
    layout->addWidget(placeholder);
    setCentralWidget(container);
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
    auto *dock = new QDockWidget(tr("场景"), this);
    dock->setObjectName(QStringLiteral("scenesDock"));
    dock->setMinimumWidth(180);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("scenesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("studioState"), m_studioState);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(QColor(QStringLiteral("#1d1d1d")));
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("场景面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("scenesLoadError"));
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
    auto *dock = new QDockWidget(tr("来源"), this);
    dock->setObjectName(QStringLiteral("sourcesDock"));
    dock->setMinimumWidth(220);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("sourcesQuickWidget"));
    view->rootContext()->setContextProperty(QStringLiteral("studioState"), m_studioState);
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(QColor(QStringLiteral("#1d1d1d")));
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("来源面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("sourcesLoadError"));
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
    auto *dock = new QDockWidget(tr("混音器"), this);
    dock->setObjectName(QStringLiteral("mixerDock"));
    dock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("mixerQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setClearColor(QColor(QStringLiteral("#1d1d1d")));
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("混音器面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("mixerLoadError"));
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

void MainWindow::setupTransitionsDock()
{
    auto *dock = new QDockWidget(tr("场景转场"), this);
    dock->setObjectName(QStringLiteral("transitionsDock"));
    dock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("transitionsQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setMinimumHeight(124);
    view->setClearColor(QColor(QStringLiteral("#1d1d1d")));
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("场景转场面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("transitionsLoadError"));
    errorLabel->setAlignment(Qt::AlignCenter);
    errorLabel->setWordWrap(true);
    pages->addWidget(errorLabel);

    const auto showError = [this, pages, errorLabel](const QString &details) {
        qWarning().noquote() << tr("场景转场面板加载失败：") << details;
        errorLabel->setToolTip(details);
        pages->setCurrentWidget(errorLabel);
        statusBar()->showMessage(tr("场景转场面板加载失败，请查看日志。"));
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
                statusBar()->showMessage(tr("场景转场面板已加载"), 3000);
            pages->setCurrentWidget(view);
        }
    });
    connect(view, &QQuickWidget::sceneGraphError, this,
            [showError](QQuickWindow::SceneGraphError, const QString &message) {
        showError(message);
    });

    dock->setWidget(pages);
    view->setSource(QUrl(QStringLiteral("qrc:/OpenLiveStudio/Ui/qml/TransitionsPanel.qml")));
}

void MainWindow::setupControlsDock()
{
    auto *dock = new QDockWidget(tr("控制"), this);
    dock->setObjectName(QStringLiteral("controlsDock"));
    dock->setMinimumWidth(200);

    auto *pages = new QStackedWidget(dock);
    auto *view = new QQuickWidget(pages);
    view->setObjectName(QStringLiteral("controlsQuickWidget"));
    view->setResizeMode(QQuickWidget::SizeRootObjectToView);
    view->setMinimumHeight(124);
    view->setClearColor(QColor(QStringLiteral("#232323")));
    pages->addWidget(view);

    auto *errorLabel = new QLabel(tr("控制面板加载失败，请查看日志。"), pages);
    errorLabel->setObjectName(QStringLiteral("controlsLoadError"));
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
