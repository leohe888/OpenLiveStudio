#include "video/videosourceaggregator.h"
#include "video/videosourcemanager.h"

#include <QSet>

VideoSourceAggregator::VideoSourceAggregator(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    const auto schedule = [this] {
        schedulePublish();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, schedule);
    connect(&manager, &VideoSourceManager::sourceChanged, this, schedule);
    schedulePublish();
}

void VideoSourceAggregator::setImageSources(const QList<PreviewSource> &sources)
{
    replaceSnapshot(m_images, sources);
}

void VideoSourceAggregator::setDisplaySources(const QList<PreviewSource> &sources)
{
    replaceSnapshot(m_displays, sources);
}

void VideoSourceAggregator::setCameraSources(const QList<PreviewSource> &sources)
{
    replaceSnapshot(m_cameras, sources);
}

void VideoSourceAggregator::replaceSnapshot(Snapshot &snapshot, const QList<PreviewSource> &sources)
{
    snapshot.clear();
    for (const auto &source : sources)
        snapshot.insert(source.id, source);
    schedulePublish();
}

void VideoSourceAggregator::schedulePublish()
{
    if (m_publishPending)
        return;
    m_publishPending = true;
    QMetaObject::invokeMethod(
        this,
        [this] {
            m_publishPending = false;
            publish();
        },
        Qt::QueuedConnection);
}

void VideoSourceAggregator::publish()
{
    QList<PreviewSource> result;
    QSet<QString> retained;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        const auto id = item.id();
        retained.insert(id);
        const auto type = item.type();
        const Snapshot *snapshot = nullptr;
        if (type == VideoSourceType::Image)
            snapshot = &m_images;
        else if (type == VideoSourceType::DisplayCapture)
            snapshot = &m_displays;
        else if (type == VideoSourceType::Camera)
            snapshot = &m_cameras;
        if (!snapshot)
            continue;
        const auto entry = snapshot->constFind(id);
        if (entry == snapshot->cend())
            continue;

        PreviewSource source = *entry;
        source.visible = item.visible();
        source.locked = item.locked();
        source.transform = item.transform();
        source.filters = item.filters();
        result.append(std::move(source));
    }
    // Release removed frames even when a controller has no further updates.
    for (auto *snapshot : {&m_images, &m_displays, &m_cameras}) {
        for (auto it = snapshot->begin(); it != snapshot->end();) {
            if (!retained.contains(it.key()))
                it = snapshot->erase(it);
            else
                ++it;
        }
    }
    emit sourcesChanged(result);
}
