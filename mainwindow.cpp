#include "mainwindow.h"

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QStatusBar>
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
    updateStatusLayout();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenusAndToolBar()
{
    auto *viewMenu = menuBar()->addMenu(tr("视图"));
    viewMenu->addAction(tr("全屏预览"))->setEnabled(false);
    viewMenu->addSeparator();
    for (const auto &name : {tr("场景"), tr("来源"), tr("混音器"),
                             tr("场景转场"), tr("控制")}) {
        viewMenu->addAction(name)->setEnabled(false);
    }
    viewMenu->addSeparator();
    viewMenu->addAction(tr("重置布局"))->setEnabled(false);

    auto *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("关于 OpenLive Studio…"))->setEnabled(false);

    auto *toolBar = addToolBar(tr("主工具栏"));
    toolBar->setObjectName(QStringLiteral("mainToolBar"));
    toolBar->setMovable(false);
    toolBar->setMinimumHeight(24);
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
