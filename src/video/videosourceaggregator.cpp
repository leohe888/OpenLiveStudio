#include "video/videosourceaggregator.h"
#include "video/videosourcemodel.h"

#include <QSet>

VideoSourceAggregator::VideoSourceAggregator(VideoSourceModel &model, QObject *parent)
    : QObject(parent), m_model(model)
{
    const auto schedule = [this] { schedulePublish(); };
    connect(&model, &QAbstractItemModel::rowsInserted, this, schedule);
    connect(&model, &QAbstractItemModel::rowsRemoved, this, schedule);
    connect(&model, &QAbstractItemModel::rowsMoved, this, schedule);
    connect(&model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_images.clear();
        m_displays.clear();
        m_cameras.clear();
    });
    connect(&model, &QAbstractItemModel::modelReset, this, schedule);
    connect(&model, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
            if (roles.isEmpty() || roles.contains(VideoSourceModel::IdRole)
                || roles.contains(VideoSourceModel::TypeRole) || roles.contains(VideoSourceModel::VisibleRole)
                || roles.contains(VideoSourceModel::LockedRole) || roles.contains(VideoSourceModel::TransformRole)
                || roles.contains(VideoSourceModel::FiltersRole) || roles.contains(VideoSourceModel::OriginalSizeRole))
                schedulePublish();
        });
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
    if (m_publishPending) return;
    m_publishPending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_publishPending = false;
        publish();
    }, Qt::QueuedConnection);
}

void VideoSourceAggregator::publish()
{
    QList<PreviewSource> result;
    QSet<QString> retained;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        const auto id = index.data(VideoSourceModel::IdRole).toString();
        retained.insert(id);
        const auto type = index.data(VideoSourceModel::TypeRole).toString();
        const Snapshot *snapshot = nullptr;
        if (type == QStringLiteral("image")) snapshot = &m_images;
        else if (type == QStringLiteral("displayCapture")) snapshot = &m_displays;
        else if (type == QStringLiteral("camera")) snapshot = &m_cameras;
        if (!snapshot) continue;
        const auto entry = snapshot->constFind(id);
        if (entry == snapshot->cend()) continue;

        PreviewSource source = *entry;
        source.visible = index.data(VideoSourceModel::VisibleRole).toBool();
        source.locked = index.data(VideoSourceModel::LockedRole).toBool();
        source.transform = index.data(VideoSourceModel::TransformRole).value<SourceTransform>();
        source.filters = index.data(VideoSourceModel::FiltersRole).value<QList<SourceFilter>>();
        result.append(std::move(source));
    }
    // Release removed frames even when a controller has no further updates.
    for (auto *snapshot : {&m_images, &m_displays, &m_cameras}) {
        for (auto it = snapshot->begin(); it != snapshot->end();) {
            if (!retained.contains(it.key())) it = snapshot->erase(it);
            else ++it;
        }
    }
    emit sourcesChanged(result);
}
