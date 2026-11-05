#ifndef DISPLAYSOURCECONTROLLER_H
#define DISPLAYSOURCECONTROLLER_H

#include "video/display/displaycaptureworker.h"
#include "graphics/rendersourcesnapshot.h"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <map>
#include <vector>

class VideoSourceManager;

class DisplaySourceController : public QObject
{
    Q_OBJECT
public:
    explicit DisplaySourceController(VideoSourceManager &manager, QObject *parent = nullptr);
    ~DisplaySourceController() override;

    void requestAdd(const QString &name, bool captureCursor, QObject *requester);
    void cancelAdd(QObject *requester);
    bool retry(const QString &sourceId);
    int activeWorkerCount() const;
signals:
    void displayAdded(QObject *requester);
    void addFailed(QObject *requester, const QString &message);
    void renderSourcesChanged(const QList<RenderSourceSnapshot> &sources);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        bool captureCursor = true, failed = false;
        quint64 generation = 0;
        std::unique_ptr<DisplayCaptureWorker> worker;
        std::shared_ptr<const CaptureFrame> frame;
    };

    void synchronizeSources();
    bool completePendingAdds();
    void collectCaptureUpdates();
    void publishRenderSources();
    void retireWorker(Entry &entry);
    VideoSourceManager &m_sourceManager;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<DisplayCaptureWorker>> m_retiringWorkers;
    struct PendingAdd {
        QString name;
        bool cursor;
        QPointer<QObject> requester;
        std::unique_ptr<DisplayCaptureWorker> worker;
    };
    std::vector<PendingAdd> m_pendingAdds;
    QTimer m_framePollTimer;
    quint64 m_generation = 0;
    bool m_updatingSources = false;
    bool m_syncPending = false;
};
#endif
