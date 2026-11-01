#ifndef CAMERASOURCECONTROLLER_H
#define CAMERASOURCECONTROLLER_H
#include "video/camera/cameracaptureworker.h"
#include "graphics/previewsource.h"
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <map>
#include <vector>
class VideoSourceManager;

class CameraSourceController : public QObject
{
    Q_OBJECT
public:
    explicit CameraSourceController(VideoSourceManager &manager, QObject *parent = nullptr);
    ~CameraSourceController() override;

    QList<CameraDevice> devices() const
    {
        return m_devices;
    }

    bool refreshing() const
    {
        return m_refreshing;
    }

    QString catalogError() const
    {
        return m_catalogError;
    }

    void refreshDevices();
    void requestModes(const QString &deviceId);

    QList<CameraMode> modes(const QString &deviceId) const
    {
        return m_modes.value(deviceId);
    }

    QString addCamera(const QString &name, const CameraConfig &config);
    bool applyProperties(const QString &sourceId, const CameraConfig &config);
    bool retry(const QString &sourceId);
    int activeWorkerCount() const;
signals:
    void devicesChanged();
    void modesChanged(const QString &deviceId, const QString &error);
    void sourcesChanged(const QList<PreviewSource> &sources);
    void sourceError(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        CameraConfig config;
        bool failed = false;
        quint64 generation = 0;
        std::unique_ptr<CameraCaptureWorker> worker;
        std::shared_ptr<const CameraFrame> frame;
    };

    void synchronize();
    void poll();
    void publish();
    void stop(Entry &entry);
    VideoSourceManager &m_sourceManager;
    QList<CameraDevice> m_devices;
    QHash<QString, QList<CameraMode>> m_modes;
    QSet<QString> m_querying;
    QString m_catalogError;
    bool m_refreshing = false, m_busy = false, m_pending = false;
    quint64 m_generation = 0;
    QTimer m_timer;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<CameraCaptureWorker>> m_retired;
    std::vector<std::unique_ptr<QThread>> m_queries;
};
#endif
