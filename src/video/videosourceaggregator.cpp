#include "video/videosourceaggregator.h"
#include "video/videosourcemanager.h"

#include <QSet>

VideoSourceAggregator::VideoSourceAggregator(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    const auto scheduleSnapshotUpdate = [this] {
        scheduleRenderSourcesPublish();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, scheduleSnapshotUpdate);
    connect(&manager, &VideoSourceManager::sourceChanged, this, scheduleSnapshotUpdate);
    scheduleRenderSourcesPublish();
}

void VideoSourceAggregator::setImageSources(const QList<RenderSourceSnapshot> &sources)
{
    replaceSnapshot(m_imageSources, sources);
}

void VideoSourceAggregator::setDisplaySources(const QList<RenderSourceSnapshot> &sources)
{
    replaceSnapshot(m_displaySources, sources);
}

void VideoSourceAggregator::setCameraSources(const QList<RenderSourceSnapshot> &sources)
{
    replaceSnapshot(m_cameraSources, sources);
}

void VideoSourceAggregator::replaceSnapshot(Snapshot &snapshot,
                                            const QList<RenderSourceSnapshot> &sources)
{
    snapshot.clear();
    for (const auto &source : sources)
        snapshot.insert(source.id, source);
    scheduleRenderSourcesPublish();
}

void VideoSourceAggregator::scheduleRenderSourcesPublish()
{
    if (m_publishPending)
        return;
    m_publishPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_publishPending = false;
            publishRenderSources();
        },
        Qt::QueuedConnection);
}

void VideoSourceAggregator::publishRenderSources()
{
    QList<RenderSourceSnapshot> result;
    QSet<QString> retainedSourceIds;
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = m_sourceManager.source(id);
        if (!item)
            continue;
        retainedSourceIds.insert(id);
        const auto type = item->type();
        const Snapshot *snapshot = nullptr;
        if (type == VideoSourceType::Image)
            snapshot = &m_imageSources;
        else if (type == VideoSourceType::DisplayCapture)
            snapshot = &m_displaySources;
        else if (type == VideoSourceType::Camera)
            snapshot = &m_cameraSources;
        if (!snapshot)
            continue;
        const auto entry = snapshot->constFind(id);
        if (entry == snapshot->cend())
            continue;

        RenderSourceSnapshot source = *entry;
        source.visible = item->visible();
        source.locked = item->locked();
        source.transform = item->transform();
        source.filters = item->filters();
        result.append(std::move(source));
    }
    // 控制器可能不再发出更新，这里也要释放已移除来源的帧，归还其纹理槽。
    for (auto *snapshot : {&m_imageSources, &m_displaySources, &m_cameraSources}) {
        for (auto it = snapshot->begin(); it != snapshot->end();) {
            if (!retainedSourceIds.contains(it.key()))
                it = snapshot->erase(it);
            else
                ++it;
        }
    }
    emit renderSourcesChanged(result);
}
