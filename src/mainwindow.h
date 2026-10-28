#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUrl>
#include <QVariantMap>

class QDockWidget;
class QLabel;
class QMenu;
class QResizeEvent;
class QQuickWidget;
class SourceListModel;
class ImageSourceController;
class DisplayCaptureController;
class CameraCaptureController;
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
    void showSourceAddDialog();
    void showSourceRemovalDialog(const QString &sourceId);
    void showSourceFiltersDialog(const QString &sourceId);
    void showSourcePropertiesDialog(const QString &sourceId);

private:
    void applyTheme();
    void configureQuickWidget(QQuickWidget *view);
    void setupMenuBar();
    void setupPreview();
    void setupStatusBar();
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
    QDockWidget *m_sourcesDock = nullptr;
    QDockWidget *m_mixerDock = nullptr;
    QDockWidget *m_controlsDock = nullptr;
    SourceListModel *m_sourceListModel = nullptr;
    ImageSourceController *m_imageSources = nullptr;
    DisplayCaptureController *m_displaySources = nullptr;
    CameraCaptureController *m_cameraSources = nullptr;
    ThemeManager &m_theme;
};
#endif // MAINWINDOW_H
