#ifndef DISPLAYSOURCECONTROLLER_H
#define DISPLAYSOURCECONTROLLER_H

#include "video/display/displaycaptureworker.h"
#include "graphics/rendersourcesnapshot.h"
#include <QObject>
#include <QTimer>
#include "video/display/displaydevices.h"
#include <map>
#include <vector>

class VideoSourceManager;

class DisplaySourceController : public QObject
{
    Q_OBJECT
public:
    explicit DisplaySourceController(VideoSourceManager &manager, QObject *parent = nullptr);
    ~DisplaySourceController() override;

    QList<DisplayDevice> displayDevices() const
    {
        return m_devices;
    }

    void refreshDisplayDevices();
    QString addDisplay(const QString &name, const QString &displayId, bool captureCursor);
    bool retry(const QString &sourceId);
    void failTransfer(const QString &sourceId, quint64 generation, const QString &reason);
    int activeWorkerCount() const;
signals:
    void displayDevicesChanged();
    void renderSourcesChanged(const QList<RenderSourceSnapshot> &sources);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        QString displayId;
        bool captureCursor = true, failed = false;
        quint64 generation = 0;
        std::unique_ptr<DisplayCaptureWorker> worker;
        std::shared_ptr<const CaptureFrame> frame;
    };

    void synchronizeSources();
    void collectCaptureUpdates();
    void publishRenderSources();
    void retireWorker(Entry &entry);
    VideoSourceManager &m_sourceManager;
    QList<DisplayDevice> m_devices;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<DisplayCaptureWorker>> m_retiringWorkers;
    QTimer m_framePollTimer;
    quint64 m_generation = 0;
    bool m_updatingSources = false;
    bool m_syncPending = false;
};
#endif
