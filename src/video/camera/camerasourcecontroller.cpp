#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemodel.h"
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>

namespace {
CameraConfig configFrom(const QString &deviceId, const QVariantMap &mode) {
    CameraConfig result; result.deviceId = deviceId;
    if (mode.value("width").toInt() > 0) {
        result.requestedResolution = {mode.value("width").toInt(), mode.value("height").toInt()};
        result.requestedFrameRate = mode.value("numerator").toInt();
        result.frameRateDenominator = mode.value("denominator").toInt();
    }
    return result;
}
}
CameraSourceController::CameraSourceController(VideoSourceModel &model, QObject *parent)
    : QObject(parent), m_model(model)
{
    const auto sync = [this] { synchronize(); };
    connect(&model, &QAbstractItemModel::rowsInserted, this, sync);
    connect(&model, &QAbstractItemModel::rowsRemoved, this, sync);
    connect(&model, &QAbstractItemModel::rowsMoved, this, sync);
    connect(&model, &QAbstractItemModel::modelReset, this, sync);
    connect(&model, &QAbstractItemModel::dataChanged, this,
        [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
            if (roles.isEmpty() || roles.contains(VideoSourceModel::CameraConfigRole)
                || roles.contains(VideoSourceModel::VisibleRole) || roles.contains(VideoSourceModel::TransformRole)
                || roles.contains(VideoSourceModel::LockedRole) || roles.contains(VideoSourceModel::FiltersRole)) synchronize();
        });
    m_timer.setInterval(16); m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CameraSourceController::poll); m_timer.start();
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}
CameraSourceController::~CameraSourceController() {
    m_timer.stop();
    for (auto &[id, entry] : m_entries) stop(entry);
    for (auto &worker : m_retired) worker->wait();
    for (auto &query : m_queries) query->wait();
}
void CameraSourceController::refreshDevices() {
    if (m_refreshing) return;
    // Refresh disconnected/inactive devices without reopening a camera that is currently in use.
    for (auto it = m_modes.begin(); it != m_modes.end();) {
        const bool active = std::any_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
            return entry.second.worker && entry.second.config.deviceId == it.key();
        });
        if (!active) it = m_modes.erase(it); else ++it;
    }
    m_refreshing = true; m_catalogError.clear(); emit devicesChanged();
    auto query = std::unique_ptr<QThread>(QThread::create([this] {
        QString error; const auto devices = cameraDevices(&error);
        QMetaObject::invokeMethod(this, [this, devices, error] {
            m_devices = devices; m_catalogError = error; m_refreshing = false; emit devicesChanged();
        }, Qt::QueuedConnection);
    }));
    query->start(); m_queries.push_back(std::move(query));
}
void CameraSourceController::requestModes(const QString &id) {
    if (id.isEmpty() || m_querying.contains(id)) return;
    // A cached catalog avoids opening a second capture session on an active device.
    if (m_modes.contains(id)) { emit modesChanged(id, {}); return; }
    m_querying.insert(id);
    auto query = std::unique_ptr<QThread>(QThread::create([this, id] {
        QString error; const auto result = cameraModes(id, &error);
        QMetaObject::invokeMethod(this, [this, id, result, error] {
            m_querying.remove(id);
            if (error.isEmpty()) m_modes.insert(id, result);
            emit modesChanged(id, error);
        }, Qt::QueuedConnection);
    }));
    query->start(); m_queries.push_back(std::move(query));
}
QString CameraSourceController::addCamera(const QString &name, const QString &id, const QVariantMap &mode) {
    if (name.trimmed().isEmpty() || id.isEmpty() || m_model.cameraDeviceInUse(id)) return {};
    const auto config = configFrom(id, mode);
    if ((!config.requestedResolution.isEmpty() && (config.requestedFrameRate <= 0 || config.frameRateDenominator <= 0))) return {};
    const QString sourceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_model.addSource({sourceId, name.trimmed(), CameraSourceData{config}})) return {};
    return sourceId;
}
bool CameraSourceController::applyProperties(const QString &sourceId, const QString &id, const QVariantMap &mode) {
    const auto old = m_model.cameraProperties(sourceId);
    if (!m_model.setCameraConfig(sourceId, configFrom(id, mode))) return false;
    // A changed config already restarts through the model notification.
    if (old == m_model.cameraProperties(sourceId)) retry(sourceId);
    return true;
}
bool CameraSourceController::retry(const QString &id) {
    auto it = m_entries.find(id); if (it == m_entries.end()) return false;
    stop(it->second); it->second.failed = false; synchronize(); return true;
}
void CameraSourceController::stop(Entry &entry) {
    entry.frame.reset();
    if (entry.worker) { entry.worker->stop(); m_retired.push_back(std::move(entry.worker)); }
}
void CameraSourceController::synchronize() {
    if (m_busy) {
        if (!m_pending) { m_pending = true; QMetaObject::invokeMethod(this, [this] { m_pending = false; synchronize(); }, Qt::QueuedConnection); }
        return;
    }
    m_busy = true; const auto reset = qScopeGuard([this] { m_busy = false; });
    QSet<QString> retained;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0);
        if (index.data(VideoSourceModel::TypeRole).toString() != "camera") continue;
        const QString id = index.data(VideoSourceModel::IdRole).toString(); retained.insert(id);
        auto &entry = m_entries[id];
        const auto values = index.data(VideoSourceModel::CameraConfigRole).toMap();
        const CameraConfig config = configFrom(values.value("deviceId").toString(), values);
        if (!(entry.config == config)) { stop(entry); entry.config = config; entry.failed = false; }
        if (!index.data(VideoSourceModel::VisibleRole).toBool()) {
            stop(entry);
            m_model.setSourceCaptureStatus(id, "paused", entry.failed
                ? m_model.cameraProperties(id).value("error").toString() : QString());
        } else if (!entry.worker && !entry.failed) {
            // Never reopen while an old session on the same device is still retiring.
            const bool retiring = std::any_of(m_retired.cbegin(), m_retired.cend(), [&](const auto &worker) {
                return worker->deviceId() == config.deviceId && !worker->isFinished();
            });
            if (retiring) { m_model.setSourceCaptureStatus(id, "starting", {}); continue; }
            entry.generation = ++m_generation;
            entry.worker = std::make_unique<CameraCaptureWorker>(config, entry.generation); entry.worker->start();
            m_model.setSourceCaptureStatus(id, "starting", {});
        } else if (entry.failed) {
            m_model.setSourceCaptureStatus(id, "error", m_model.cameraProperties(id).value("error").toString());
        }
    }
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!retained.contains(it->first)) { stop(it->second); it = m_entries.erase(it); } else ++it;
    }
    publish();
}
void CameraSourceController::poll() {
    if (m_busy) return;
    bool changed = false;
    m_busy = true;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker) continue;
        const QString error = entry.worker->error();
        if (!error.isEmpty()) {
            stop(entry); entry.failed = true; m_model.setSourceCaptureStatus(id, "error", error);
            emit sourceError(id, error); changed = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation) continue;
            entry.frame = std::move(frame);
            m_model.setCameraActualMode(id, entry.frame->image.size(), entry.frame->rateNumerator, entry.frame->rateDenominator);
            m_model.setSourceCaptureStatus(id, "capturing", {}); changed = true;
        }
    }
    const bool retired = !m_retired.empty();
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), [](const auto &worker) { return worker->isFinished(); }), m_retired.end());
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(), [](const auto &thread) { return thread->isFinished(); }), m_queries.end());
    m_busy = false;
    if (retired) synchronize(); else if (changed) publish();
}
void CameraSourceController::publish() {
    QList<PreviewSource> result;
    for (int row = 0; row < m_model.rowCount(); ++row) {
        const auto index = m_model.index(row, 0); const QString id = index.data(VideoSourceModel::IdRole).toString();
        if (index.data(VideoSourceModel::TypeRole).toString() != "camera") continue;
        PreviewSource source; source.id = id;
        const auto camera = m_entries.find(id);
        if (camera == m_entries.end()) continue;
        if (camera->second.frame) source.image = camera->second.frame->image;
        source.visible = index.data(VideoSourceModel::VisibleRole).toBool(); source.locked = index.data(VideoSourceModel::LockedRole).toBool();
        source.transform = index.data(VideoSourceModel::TransformRole).value<SourceTransform>();
        source.filters = index.data(VideoSourceModel::FiltersRole).value<QList<SourceFilter>>(); result.append(std::move(source));
    }
    emit sourcesChanged(result);
}
int CameraSourceController::activeWorkerCount() const {
    int count = 0;
    for (const auto &[id, entry] : m_entries) if (entry.worker && !entry.worker->isFinished()) ++count;
    for (const auto &worker : m_retired) if (!worker->isFinished()) ++count;
    return count;
}
