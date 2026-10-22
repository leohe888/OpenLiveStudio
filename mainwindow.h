#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class QLabel;
class QResizeEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

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
    void updateStatusLayout();

    QLabel *m_outputStatus = nullptr;
    QLabel *m_outputMetrics = nullptr;
};
#endif // MAINWINDOW_H
