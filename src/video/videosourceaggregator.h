#ifndef VIDEOSOURCEAGGREGATOR_H
#define VIDEOSOURCEAGGREGATOR_H

#include "graphics/previewsource.h"

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
    void setImageSources(const QList<PreviewSource> &sources);
    void setDisplaySources(const QList<PreviewSource> &sources);
    void setCameraSources(const QList<PreviewSource> &sources);

signals:
    void sourcesChanged(const QList<PreviewSource> &sources);

private:
    using Snapshot = QHash<QString, PreviewSource>;
    void replaceSnapshot(Snapshot &snapshot, const QList<PreviewSource> &sources);
    void schedulePublish();
    void publish();

    VideoSourceManager &m_sourceManager;
    Snapshot m_images;
    Snapshot m_displays;
    Snapshot m_cameras;
    bool m_publishPending = false;
};

#endif // VIDEOSOURCEAGGREGATOR_H
