#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUrl>
#include <QVariantMap>

class QLabel;
class QMenu;
class QResizeEvent;
class QQuickWidget;
class StudioState;
class ThemeManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // The caller owns theme and must keep it alive until this window is destroyed.
    explicit MainWindow(ThemeManager &theme, QWidget *parent = nullptr);
    ~MainWindow() override;

    void showDialog(const QUrl &source, const QString &title,
                    const QVariantMap &properties = {});

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void showSceneNameDialog(const QString &sceneId, const QString &initialName);
    void showSceneRemovalDialog(const QString &sceneId);
    void showSourceAddDialog(const QString &sceneId);
    void showSourceRemovalDialog(const QString &sceneId, const QString &sourceId);

private:
    void applyTheme();
    void configureQuickWidget(QQuickWidget *view);
    void setupMenuBar();
    void setupPreview();
    void setupStatusBar();
    void setupScenesDock();
    void setupSourcesDock();
    void setupMixerDock();
    void setupControlsDock();
    void setupViewActions();
    void setupThemeActions();
    void setupHelpActions();
    void resetDockLayout();
    void updateStatusLayout();

    QLabel *m_outputStatus = nullptr;
    QLabel *m_outputMetrics = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_themeMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    StudioState *m_studioState = nullptr;
    ThemeManager &m_theme;
};
#endif // MAINWINDOW_H
