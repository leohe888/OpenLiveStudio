#include "mainwindow.h"

#include <QAction>
#include <QDebug>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QQmlError>
#include <QQuickWidget>
#include <QResizeEvent>
#include <QStatusBar>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
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
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenusAndToolBar()
{
    m_viewMenu = menuBar()->addMenu(tr("视图"));
    m_viewMenu->setObjectName(QStringLiteral("viewMenu"));
    m_viewMenu->addAction(tr("全屏预览"))->setEnabled(false);
    m_viewMenu->addSeparator();

    auto *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("关于 OpenLive Studio…"))->setEnabled(false);

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
