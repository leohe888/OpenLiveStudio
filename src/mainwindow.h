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
    void resizeEvent(QResizeEvent *event) override;
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
    void setupMenuBar();
    void setupPreview();
    void setupStatusBar();
    void setupSourcesDock();
    void setupMixerDock();
    void setupControlsDock();
    void setupViewActions();
    void resetDockLayout();
    void updateStatusLayout();
    void updateOutputStatus();

    QLabel *m_outputStatus = nullptr;
    QLabel *m_outputMetrics = nullptr;
    QMenu *m_viewMenu = nullptr;
    QDockWidget *m_sourcesDock = nullptr;
    QDockWidget *m_mixerDock = nullptr;
    QDockWidget *m_controlsDock = nullptr;
    SourceListModel *m_sourceListModel = nullptr;
    ImageSourceController *m_imageSources = nullptr;
    DisplayCaptureController *m_displaySources = nullptr;
    CameraCaptureController *m_cameraSources = nullptr;
    AudioSourceModel *m_audioSources = nullptr;
    RecordingController *m_recording = nullptr;
    StreamingController *m_streaming = nullptr;
    OutputSettings *m_outputSettings = nullptr;
    bool m_closePending = false;
    QString m_previousRecordingError,m_previousStreamingError;
};
#endif // MAINWINDOW_H
