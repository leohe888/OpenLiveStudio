#ifndef VIDEOSOURCEAGGREGATOR_H
#define VIDEOSOURCEAGGREGATOR_H

#include "graphics/rendersourcesnapshot.h"

#include <QHash>
#include <QObject>

class VideoSourceManager;

// GUI-thread snapshots from each controller are combined in manager order.
class VideoSourceAggregator : public QObject
{
    Q_OBJECT
public:
    explicit VideoSourceAggregator(VideoSourceManager &manager, QObject *parent = nullptr);

    // Each update replaces the complete snapshot for that source type.
    void setImageSources(const QList<RenderSourceSnapshot> &sources);
    void setDisplaySources(const QList<RenderSourceSnapshot> &sources);
    void setCameraSources(const QList<RenderSourceSnapshot> &sources);

signals:
    void renderSourcesChanged(const QList<RenderSourceSnapshot> &sources);

private:
    using Snapshot = QHash<QString, RenderSourceSnapshot>;
    void replaceSnapshot(Snapshot &snapshot, const QList<RenderSourceSnapshot> &sources);
    void scheduleRenderSourcesPublish();
    void publishRenderSources();

    VideoSourceManager &m_sourceManager;
    Snapshot m_imageSources;
    Snapshot m_displaySources;
    Snapshot m_cameraSources;
    bool m_publishPending = false;
};

#endif // VIDEOSOURCEAGGREGATOR_H
