#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUrl>
#include <QVariantMap>

class QLabel;
class QMenu;
class QResizeEvent;
class StudioState;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showDialog(const QUrl &source, const QString &title,
                    const QVariantMap &properties = {});

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void setupMenusAndToolBar();
    void setupPreview();
    void setupStatusBar();
    void setupScenesDock();
    void setupSourcesDock();
    void setupMixerDock();
    void setupTransitionsDock();
    void setupControlsDock();
    void setupDockViewActions();
    void resetDockLayout();
    void updateStatusLayout();

    QLabel *m_outputStatus = nullptr;
    QLabel *m_outputMetrics = nullptr;
    QMenu *m_viewMenu = nullptr;
    StudioState *m_studioState = nullptr;
};
#endif // MAINWINDOW_H
