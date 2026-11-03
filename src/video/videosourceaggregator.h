#ifndef VIDEOSOURCEAGGREGATOR_H
#define VIDEOSOURCEAGGREGATOR_H

#include "graphics/rendersourcesnapshot.h"

#include <QHash>
#include <QObject>

class VideoSourceManager;

// 在界面线程按来源管理器的层级顺序合并快照，渲染端无需查询各个控制器。
class VideoSourceAggregator : public QObject
{
    Q_OBJECT
public:
    explicit VideoSourceAggregator(VideoSourceManager &manager, QObject *parent = nullptr);

    // 每次更新替换该类型的完整快照，缺席的来源不会沿用旧数据。
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

#endif
