#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemanager.h"
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>

CameraSourceController::CameraSourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    const auto sync = [this] {
        synchronize();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, sync);
    connect(&manager, &VideoSourceManager::sourceChanged, this, sync);
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &CameraSourceController::poll);
    m_timer.start();
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}

CameraSourceController::~CameraSourceController()
{
    m_timer.stop();
    for (auto &[id, entry] : m_entries)
        stop(entry);
    for (auto &worker : m_retired)
        worker->wait();
    for (auto &query : m_queries)
        query->wait();
}

void CameraSourceController::refreshDevices()
{
    if (m_refreshing)
        return;
    // Refresh disconnected/inactive devices without reopening a camera that is currently in use.
    for (auto it = m_modes.begin(); it != m_modes.end();) {
        const bool active =
            std::any_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
                return entry.second.worker && entry.second.config.deviceId == it.key();
            });
        if (!active)
            it = m_modes.erase(it);
        else
            ++it;
    }
    m_refreshing = true;
    m_catalogError.clear();
    emit devicesChanged();
    auto query = std::unique_ptr<QThread>(QThread::create([this] {
        QString error;
        const auto devices = cameraDevices(&error);
        QMetaObject::invokeMethod(
            this,
            [this, devices, error] {
                m_devices = devices;
                m_catalogError = error;
                m_refreshing = false;
                emit devicesChanged();
            },
            Qt::QueuedConnection);
    }));
    query->start();
    m_queries.push_back(std::move(query));
}

void CameraSourceController::requestModes(const QString &deviceId)
{
    if (deviceId.isEmpty() || m_querying.contains(deviceId))
        return;
    // A cached catalog avoids opening a second capture session on an active device.
    if (m_modes.contains(deviceId)) {
        emit modesChanged(deviceId, {});
        return;
    }
    m_querying.insert(deviceId);
    auto query = std::unique_ptr<QThread>(QThread::create([this, deviceId] {
        QString error;
        const auto result = cameraModes(deviceId, &error);
        QMetaObject::invokeMethod(
            this,
            [this, deviceId, result, error] {
                m_querying.remove(deviceId);
                if (error.isEmpty())
                    m_modes.insert(deviceId, result);
                emit modesChanged(deviceId, error);
            },
            Qt::QueuedConnection);
    }));
    query->start();
    m_queries.push_back(std::move(query));
}

QString CameraSourceController::addCamera(const QString &name, const CameraSourceConfig &config)
{
    if (name.trimmed().isEmpty() || !config.isValid() ||
        m_sourceManager.cameraDeviceInUse(config.deviceId))
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_sourceManager.addSource({id, name.trimmed(), config}))
        return {};
    return id;
}

bool CameraSourceController::applyProperties(const QString &sourceId, const CameraSourceConfig &config)
{
    const auto *camera = m_sourceManager.cameraSource(sourceId);
    if (!camera)
        return false;
    const bool unchanged = (*camera) == config;
    if (!m_sourceManager.setCameraConfig(sourceId, config))
        return false;
    // Changed configuration already restarts through the manager notification.
    if (unchanged)
        retry(sourceId);
    return true;
}

bool CameraSourceController::retry(const QString &sourceId)
{
    auto it = m_entries.find(sourceId);
    if (it == m_entries.end())
        return false;
    stop(it->second);
    it->second.failed = false;
    synchronize();
    return true;
}

void CameraSourceController::stop(Entry &entry)
{
    entry.frame.reset();
    if (entry.worker) {
        entry.worker->stop();
        m_retired.push_back(std::move(entry.worker));
    }
}

void CameraSourceController::synchronize()
{
    if (m_busy) {
        if (!m_pending) {
            m_pending = true;
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_pending = false;
                    synchronize();
                },
                Qt::QueuedConnection);
        }
        return;
    }
    m_busy = true;
    const auto reset = qScopeGuard([this] {
        m_busy = false;
    });
    QSet<QString> retained;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        if (item.type() != VideoSourceType::Camera)
            continue;
        const QString id = item.id;
        retained.insert(id);
        auto &entry = m_entries[id];
        const auto &config = std::get<CameraSourceConfig>(item.config);
        if (!(entry.config == config)) {
            stop(entry);
            entry.config = config;
            entry.failed = false;
        }
        if (!item.visible) {
            stop(entry);
            m_sourceManager.setSourceStatus(
                id, "paused",
                entry.failed ? m_sourceManager.sourceStatus(id).error
                             : QString());
        } else if (!entry.worker && !entry.failed) {
            // Never reopen while an old session on the same device is still retiring.
            const bool retiring =
                std::any_of(m_retired.cbegin(), m_retired.cend(), [&](const auto &worker) {
                    return worker->deviceId() == config.deviceId && !worker->isFinished();
                });
            if (retiring) {
                m_sourceManager.setSourceStatus(id, "starting", {});
                continue;
            }
            entry.generation = ++m_generation;
            entry.worker = std::make_unique<CameraCaptureWorker>(config, entry.generation);
            entry.worker->start();
            m_sourceManager.setSourceStatus(id, "starting", {});
        } else if (entry.failed) {
            m_sourceManager.setSourceStatus(
                id, "error", m_sourceManager.sourceStatus(id).error);
        }
    }
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!retained.contains(it->first)) {
            stop(it->second);
            it = m_entries.erase(it);
        } else
            ++it;
    }
    publish();
}

void CameraSourceController::poll()
{
    if (m_busy)
        return;
    bool changed = false;
    m_busy = true;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker)
            continue;
        const QString error = entry.worker->lastError();
        if (!error.isEmpty()) {
            stop(entry);
            entry.failed = true;
            m_sourceManager.setSourceStatus(id, "error", error);
            emit sourceFailed(id, error);
            changed = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation)
                continue;
            entry.frame = std::move(frame);
            m_sourceManager.setCameraActualMode(id, entry.frame->image.size(),
                                                entry.frame->rateNumerator,
                                                entry.frame->rateDenominator);
            m_sourceManager.setSourceStatus(id, "capturing", {});
            changed = true;
        }
    }
    const bool retired = !m_retired.empty();
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retired.end());
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(),
                                   [](const auto &thread) {
                                       return thread->isFinished();
                                   }),
                    m_queries.end());
    m_busy = false;
    if (retired)
        synchronize();
    else if (changed)
        publish();
}

void CameraSourceController::publish()
{
    QList<PreviewSource> result;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        const QString id = item.id;
        if (item.type() != VideoSourceType::Camera)
            continue;
        PreviewSource source;
        source.id = id;
        const auto camera = m_entries.find(id);
        if (camera == m_entries.end())
            continue;
        if (camera->second.frame)
            source.image = camera->second.frame->image;
        source.visible = item.visible;
        source.locked = item.locked;
        source.canvasGeometry = item.canvasGeometry;
        source.filters = item.filters;
        result.append(std::move(source));
    }
    emit sourcesChanged(result);
}

int CameraSourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &[id, entry] : m_entries)
        if (entry.worker && !entry.worker->isFinished())
            ++count;
    for (const auto &worker : m_retired)
        if (!worker->isFinished())
            ++count;
    return count;
}
