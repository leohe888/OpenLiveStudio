#include "displaycapturecontroller.h"
#include "displaytargets.h"
#include "core/sourcelistmodel.h"
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
#include <algorithm>

DisplayCaptureController::DisplayCaptureController(SourceListModel &model, QObject *parent)
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
            if (roles.isEmpty() || roles.contains(SourceListModel::DisplayConfigRole)
                || roles.contains(SourceListModel::VisibleRole) || roles.contains(SourceListModel::TransformRole)
                || roles.contains(SourceListModel::LockedRole) || roles.contains(SourceListModel::FiltersRole)) synchronize();
        });
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &DisplayCaptureController::poll);
    m_timer.start();
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}

DisplayCaptureController::~DisplayCaptureController()
{
    m_timer.stop();
    for (auto &entry : m_entries) stop(entry.second);
    // App shutdown only. Normal deletion retires workers without a GUI-thread join.
    for (auto &worker : m_retired) worker->wait();
}

void DisplayCaptureController::refreshDisplays()
{
    QVariantList targets;
    for (const auto &target : displayTargets())
        targets.append(QVariantMap{{"id", target.id}, {"name", target.name}});
    if (targets != m_displays) { m_displays = targets; emit displaysChanged(); }
}

QString DisplayCaptureController::addDisplay(const QString &name, const QString &displayId, bool cursor)
{
    if (name.trimmed().isEmpty() || displayId.trimmed().isEmpty()) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keep the source even if opening the selected display subsequently fails.
    if (!m_model.addSource({id, name.trimmed(), DisplayCaptureConfig{displayId, cursor}})) return {};
    return id;
}

bool DisplayCaptureController::applyProperties(const QString &id, const QString &displayId, bool cursor)
{
    const auto old = m_model.displayProperties(id);
    if (!m_model.setDisplayConfig(id, displayId, cursor)) return false;
    if (old.value("displayId").toString() == displayId && old.value("captureCursor").toBool() == cursor)
        retry(id);
    return true;
}

bool DisplayCaptureController::retry(const QString &id)
{
    auto it = m_entries.find(id);
    if (it == m_entries.end()) return false;
    stop(it->second);
    it->second.failed = false;
    it->second.cpu = false;
    synchronize();
    return true;
}

void DisplayCaptureController::stop(Entry &entry)
{
    entry.frame.reset();
    if (!entry.worker) return;
    entry.worker->stop();
    m_retired.push_back(std::move(entry.worker));
}

void DisplayCaptureController::synchronize()
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
        if (index.data(SourceListModel::TypeRole).toString() != "displayCapture") continue;
        const QString id = index.data(SourceListModel::IdRole).toString();
        const auto config = index.data(SourceListModel::DisplayConfigRole).toMap();
        retained.insert(id);
        auto &entry = m_entries[id];
        const QString displayId = config.value("displayId").toString();
        const bool cursor = config.value("captureCursor").toBool();
        if (entry.displayId != displayId || entry.cursor != cursor) {
            stop(entry);
            entry.displayId = displayId;
            entry.cursor = cursor;
            entry.failed = false;
            entry.cpu = false;
        }
        if (!index.data(SourceListModel::VisibleRole).toBool()) {
            stop(entry);
            m_model.setSourceCaptureStatus(id, "paused", entry.failed ? m_model.displayProperties(id).value("error").toString() : QString());
        } else if (!entry.worker && !entry.failed) {
            entry.generation = ++m_generation;
            entry.worker = std::make_unique<DisplayCaptureWorker>(displayId, cursor, entry.generation);
            entry.cpu = qEnvironmentVariableIsSet("OPENLIVE_FORCE_CPU_CAPTURE");
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

void DisplayCaptureController::setImageSources(const QList<PreviewSource> &images)
{
    m_images = images;
    if (!m_busy) publish();
}

void DisplayCaptureController::useCpu(const QString &id, quint64 generation, const QString &reason)
{
    const auto it = m_entries.find(id);
    if (it == m_entries.end() || it->second.generation != generation || !it->second.worker || it->second.cpu) return;
    auto &entry = it->second;
    entry.cpu = true;
    entry.frame.reset();
    entry.worker->useCpu();
    qWarning().noquote() << "Capture CPU fallback:" << id << reason;
    publish();
}

void DisplayCaptureController::poll()
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
            if (frame->generation != entry.generation || (entry.cpu && frame->gpu)) continue;
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

void DisplayCaptureController::publish()
{
    QList<PreviewSource> result;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        const QString id = index.data(SourceListModel::IdRole).toString();
        PreviewSource source;
        source.id = id;
        const auto dynamic = m_entries.find(id);
        if (dynamic != m_entries.end()) source.frame = dynamic->second.frame;
        else {
            const auto image = std::find_if(m_images.cbegin(), m_images.cend(), [&](const auto &entry) { return entry.id == id; });
            if (image == m_images.cend()) continue;
            source.image = image->image;
        }
        source.visible = index.data(SourceListModel::VisibleRole).toBool();
        source.locked = index.data(SourceListModel::LockedRole).toBool();
        source.transform = index.data(SourceListModel::TransformRole).value<SourceTransform>();
        source.filters = index.data(SourceListModel::FiltersRole).value<QList<SourceFilter>>();
        result.append(std::move(source));
    }
    emit sourcesChanged(result);
}

int DisplayCaptureController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries) if (entry.second.worker && !entry.second.worker->isFinished()) ++count;
    for (const auto &worker : m_retired) if (!worker->isFinished()) ++count;
    return count;
}
