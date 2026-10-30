#include "video/display/displaysourcecontroller.h"
#include "video/display/displaytargets.h"
#include "video/videosourcemodel.h"
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
#include <algorithm>

DisplaySourceController::DisplaySourceController(VideoSourceModel &model, QObject *parent)
    : QObject(parent), m_model(model)
{
    refreshDisplays();
    const auto sync = [this] { synchronize(); };
    connect(&model, &QAbstractItemModel::rowsInserted, this, sync);
    connect(&model, &QAbstractItemModel::rowsRemoved, this, sync);
    connect(&model, &QAbstractItemModel::rowsMoved, this, sync);
    connect(&model, &QAbstractItemModel::modelReset, this, sync);
    connect(&model, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
            if (roles.isEmpty() || roles.contains(VideoSourceModel::DisplayConfigRole)
                || roles.contains(VideoSourceModel::VisibleRole) || roles.contains(VideoSourceModel::TransformRole)
                || roles.contains(VideoSourceModel::LockedRole) || roles.contains(VideoSourceModel::FiltersRole)) synchronize();
        });
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &DisplaySourceController::poll);
    m_timer.start();
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}

DisplaySourceController::~DisplaySourceController()
{
    m_timer.stop();
    for (auto &entry : m_entries) stop(entry.second);
    // App shutdown only. Normal deletion retires workers without a GUI-thread join.
    for (auto &worker : m_retired) worker->wait();
}

void DisplaySourceController::refreshDisplays()
{
    QVariantList targets;
    for (const auto &target : displayTargets())
        targets.append(QVariantMap{{"id", target.id}, {"name", target.name}});
    if (targets != m_displays) { m_displays = targets; emit displaysChanged(); }
}

QString DisplaySourceController::addDisplay(const QString &name, const QString &displayId, bool cursor)
{
    if (name.trimmed().isEmpty() || displayId.trimmed().isEmpty()) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keep the source even if opening the selected display subsequently fails.
    if (!m_model.addSource({id, name.trimmed(), DisplayCaptureConfig{displayId, cursor}})) return {};
    return id;
}

bool DisplaySourceController::applyProperties(const QString &id, const QString &displayId, bool cursor)
{
    const auto old = m_model.displayProperties(id);
    if (!m_model.setDisplayConfig(id, displayId, cursor)) return false;
    if (old.value("displayId").toString() == displayId && old.value("captureCursor").toBool() == cursor)
        retry(id);
    return true;
}

bool DisplaySourceController::retry(const QString &id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end()) return false;
    stop(it->second);
    it->second.failed = false;
    synchronize();
    return true;
}

void DisplaySourceController::stop(Entry &entry)
{
    entry.frame.reset();
    if (!entry.worker) return;
    entry.worker->stop();
    m_retired.push_back(std::move(entry.worker));
}

void DisplaySourceController::synchronize()
{
    if (m_busy) {
        if (!m_syncPending) {
            m_syncPending = true;
            QMetaObject::invokeMethod(this, [this] { m_syncPending = false; synchronize(); }, Qt::QueuedConnection);
        }
        return;
    }
    m_busy = true;
    const auto reset = qScopeGuard([this] { m_busy = false; });
    QSet<QString> retained;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        if (index.data(VideoSourceModel::TypeRole).toString() != "displayCapture") continue;
        const QString id = index.data(VideoSourceModel::IdRole).toString();
        const auto config = index.data(VideoSourceModel::DisplayConfigRole).toMap();
        retained.insert(id);
        auto &entry = m_entries[id];
        const QString displayId = config.value("displayId").toString();
        const bool cursor = config.value("captureCursor").toBool();
        if (entry.displayId != displayId || entry.cursor != cursor) {
            stop(entry);
            entry.displayId = displayId;
            entry.cursor = cursor;
            entry.failed = false;
        }
        if (!index.data(VideoSourceModel::VisibleRole).toBool()) {
            stop(entry);
            m_model.setSourceCaptureStatus(id, "paused", entry.failed ? m_model.displayProperties(id).value("error").toString() : QString());
        } else if (!entry.worker && !entry.failed) {
            entry.generation = ++m_generation;
            entry.worker = std::make_unique<DisplayCaptureWorker>(displayId, cursor, entry.generation);
            entry.worker->start();
            m_model.setSourceCaptureStatus(id, "starting", {});
        } else if (entry.failed) {
            m_model.setSourceCaptureStatus(id, "error", m_model.displayProperties(id).value("error").toString());
        }
    }
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!retained.contains(it->first)) { stop(it->second); it = m_entries.erase(it); }
        else ++it;
    }
    publish();
}

void DisplaySourceController::setImageSources(const QList<PreviewSource> &images)
{
    m_images = images;
    if (!m_busy) publish();
}

void DisplaySourceController::failTransfer(const QString &id, quint64 generation, const QString &reason)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || it->second.generation != generation || !it->second.worker || it->second.failed) return;
    auto &entry = it->second;
    entry.failed = true;
    stop(entry);
    const QString message = tr("GPU 画面传输失败，已停止采集。请检查显卡驱动后手动重试。");
    m_model.setSourceCaptureStatus(id, "error", message);
    qWarning().noquote() << "Display GPU transfer failed:" << id << reason;
    emit sourceError(id, message);
    publish();
}

void DisplaySourceController::poll()
{
    if (m_busy) return;
    m_busy = true;
    const auto reset = qScopeGuard([this] { m_busy = false; });
    bool changed = false;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker) continue;
        const QString error = entry.worker->error();
        if (!error.isEmpty()) {
            entry.failed = true;
            stop(entry);
            m_model.setSourceCaptureStatus(id, "error", error);
            emit sourceError(id, error);
            changed = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation) continue;
            entry.frame = std::move(frame);
            m_model.setSourceOriginalSize(id, entry.frame->size);
            m_model.setSourceCaptureStatus(id, "capturing", {});
            changed = true;
        }
    }
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
        [](const auto &worker) { return worker->isFinished(); }), m_retired.end());
    if (changed) publish();
}

void DisplaySourceController::publish()
{
    QList<PreviewSource> result;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        const QString id = index.data(VideoSourceModel::IdRole).toString();
        PreviewSource source;
        source.id = id;
        const auto dynamic = m_entries.find(id);
        if (dynamic != m_entries.end()) source.frame = dynamic->second.frame;
        else {
            const auto image = std::find_if(m_images.cbegin(), m_images.cend(), [&](const auto &entry) { return entry.id == id; });
            if (image == m_images.cend()) continue;
            source.image = image->image;
        }
        source.visible = index.data(VideoSourceModel::VisibleRole).toBool();
        source.locked = index.data(VideoSourceModel::LockedRole).toBool();
        source.transform = index.data(VideoSourceModel::TransformRole).value<SourceTransform>();
        source.filters = index.data(VideoSourceModel::FiltersRole).value<QList<SourceFilter>>();
        result.append(std::move(source));
    }
    emit sourcesChanged(result);
}

int DisplaySourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries) if (entry.second.worker && !entry.second.worker->isFinished()) ++count;
    for (const auto &worker : m_retired) if (!worker->isFinished()) ++count;
    return count;
}
