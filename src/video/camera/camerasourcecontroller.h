#ifndef CAMERASOURCECONTROLLER_H
#define CAMERASOURCECONTROLLER_H
#include "video/camera/cameracaptureworker.h"
#include "graphics/rendersourcesnapshot.h"
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
        return m_deviceModes.value(deviceId);
    }

    QString addCamera(const QString &name, const CameraSourceConfig &config);
    bool retry(const QString &sourceId);
    int activeWorkerCount() const;
signals:
    void devicesChanged();
    void modesChanged(const QString &deviceId, const QString &error);
    void renderSourcesChanged(const QList<RenderSourceSnapshot> &sources);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        CameraSourceConfig config;
        bool failed = false;
        quint64 generation = 0;
        std::unique_ptr<CameraCaptureWorker> worker;
        std::shared_ptr<const CameraFrame> frame;
    };

    void synchronizeSources();
    void collectCaptureUpdates();
    void publishRenderSources();
    void retireWorker(Entry &entry);
    VideoSourceManager &m_sourceManager;
    QList<CameraDevice> m_devices;
    QHash<QString, QList<CameraMode>> m_deviceModes;
    QSet<QString> m_pendingModeDeviceIds;
    QString m_catalogError;
    bool m_refreshing = false, m_updatingSources = false, m_syncPending = false;
    quint64 m_generation = 0;
    QTimer m_framePollTimer;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<CameraCaptureWorker>> m_retiringWorkers;
    std::vector<std::unique_ptr<QThread>> m_queryThreads;
};
#endif
