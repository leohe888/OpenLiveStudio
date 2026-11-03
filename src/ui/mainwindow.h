#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class PreviewWidget;
class QDockWidget;
class QLabel;
class VideoSourceManager;
class VideoSourceAggregator;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;
class AudioSourceManager;
class RecordingController;
class StreamingController;
class OutputSettings;
class QCloseEvent;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showSourceAddDialog();
    void showSourceRemovalDialog(const QString &sourceId);
    void showSourceFiltersDialog(const QString &sourceId);
    void showAudioSourceAddDialog();
    void showAudioSourceRemovalDialog(const QString &sourceId);
    void showOutputSettingsDialog();
    void toggleRecording();
    void toggleStreaming();

private:
    void setupSourceControllers();
    void setupOutputControllers();
    void connectSourceErrors();
    void setupPreview();
    void setupStatusBar();
    void setupVideoSourcesDock();
    void setupAudioSourcesDock();
    void setupControlsDock();
    void setupDockLayout();
    void updateOutputStatus();
    PreviewWidget *m_preview = nullptr;
    QDockWidget *m_videoSourceDock = nullptr;
    QDockWidget *m_audioSourceDock = nullptr;
    QDockWidget *m_controlDock = nullptr;
    QLabel *m_outputStatusLabel = nullptr;
    VideoSourceManager *m_videoSourceManager = nullptr;
    VideoSourceAggregator *m_videoSourceAggregator = nullptr;
    ImageSourceController *m_imageSourceController = nullptr;
    DisplaySourceController *m_displaySourceController = nullptr;
    CameraSourceController *m_cameraSourceController = nullptr;
    AudioSourceManager *m_audioSourceManager = nullptr;
    RecordingController *m_recordingController = nullptr;
    StreamingController *m_streamingController = nullptr;
    OutputSettings *m_outputSettings = nullptr;
    bool m_closePending = false;
    QString m_previousRecordingError, m_previousStreamingError;
};
#endif
