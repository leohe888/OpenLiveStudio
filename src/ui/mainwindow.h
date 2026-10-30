#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QUrl>
#include <QVariantMap>

class QDockWidget;
class QLabel;
class QQuickWidget;
class VideoSourceModel;
class VideoSourceAggregator;
class ImageSourceController;
class DisplaySourceController;
class CameraSourceController;
class AudioSourceModel;
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

    void showDialog(const QUrl &source, const QString &title,
                    const QVariantMap &properties = {});

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showSourceAddDialog();
    void showSourceRemovalDialog(const QString &sourceId);
    void showSourceFiltersDialog(const QString &sourceId);
    void showSourcePropertiesDialog(const QString &sourceId);
    void showAudioSourceAddDialog();
    void showAudioSourcePropertiesDialog(const QString &sourceId);
    void showAudioSourceRemovalDialog(const QString &sourceId);
    void showOutputSettingsDialog();
    void browseRecordingDirectory();
    void toggleRecording();
    void toggleStreaming();

private:
    void applyColors();
    void configureQuickWidget(QQuickWidget *view);
    void setupPreview();
    void setupStatusBar();
    void setupSourcesDock();
    void setupMixerDock();
    void setupControlsDock();
    void setupDockLayout();
    void updateOutputStatus();
    QDockWidget *m_videoSourceDock = nullptr;
    QDockWidget *m_audioSourceDock = nullptr;
    QDockWidget *m_controlDock = nullptr;
    QLabel *m_outputStatus = nullptr;
    VideoSourceModel *m_videoSourceModel = nullptr;
    VideoSourceAggregator *m_videoSourceAggregator = nullptr;
    ImageSourceController *m_imageSourceController = nullptr;
    DisplaySourceController *m_displaySourceController = nullptr;
    CameraSourceController *m_cameraSourceController = nullptr;
    AudioSourceModel *m_audioSourceModel = nullptr;
    RecordingController *m_recordingController = nullptr;
    StreamingController *m_streamingController = nullptr;
    OutputSettings *m_outputSettings = nullptr;
    bool m_closePending = false;
    QString m_previousRecordingError,m_previousStreamingError;
};
#endif // MAINWINDOW_H
