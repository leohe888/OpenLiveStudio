#ifndef DISPLAYSOURCECONTROLLER_H
#define DISPLAYSOURCECONTROLLER_H

#include "video/display/displaycaptureworker.h"
#include "graphics/previewsource.h"
#include <QObject>
#include <QTimer>
#include "video/display/displaytargets.h"
#include <map>
#include <vector>

class VideoSourceManager;

class DisplaySourceController : public QObject
{
    Q_OBJECT
public:
    explicit DisplaySourceController(VideoSourceManager &manager, QObject *parent = nullptr);
    ~DisplaySourceController() override;

    QList<DisplayTarget> displays() const
    {
        return m_displays;
    }

    void refreshDisplays();
    QString addDisplay(const QString &name, const QString &displayId, bool cursor);
    bool applyProperties(const QString &sourceId, const QString &displayId, bool cursor);
    bool retry(const QString &sourceId);
    void failTransfer(const QString &sourceId, quint64 generation, const QString &reason);
    int activeWorkerCount() const;
signals:
    void displaysChanged();
    void sourcesChanged(const QList<PreviewSource> &sources);
    void sourceFailed(const QString &sourceId, const QString &message);

private:
    struct Entry
    {
        QString displayId;
        bool cursor = true, failed = false;
        quint64 generation = 0;
        std::unique_ptr<DisplayCaptureWorker> worker;
        std::shared_ptr<const CaptureFrame> frame;
    };

    void synchronize();
    void poll();
    void publish();
    void stop(Entry &entry);
    VideoSourceManager &m_sourceManager;
    QList<DisplayTarget> m_displays;
    std::map<QString, Entry> m_entries;
    std::vector<std::unique_ptr<DisplayCaptureWorker>> m_retired;
    QTimer m_timer;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_syncPending = false;
};
#endif
