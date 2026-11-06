#include "video/camera/camerasourcecontroller.h"
#include "video/videosourcemanager.h"
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include "util/mediaclock.h"

CameraSourceController::CameraSourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    const auto synchronizeSourcesFromManager = [this] {
        synchronizeSources();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, synchronizeSourcesFromManager);
    connect(&manager, &VideoSourceManager::sourceChanged, this, synchronizeSourcesFromManager);
    m_framePollTimer.setInterval(16);
    m_framePollTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_framePollTimer, &QTimer::timeout, this,
            &CameraSourceController::collectCaptureUpdates);
    m_framePollTimer.start();
    QMetaObject::invokeMethod(this, synchronizeSourcesFromManager, Qt::QueuedConnection);
}

CameraSourceController::~CameraSourceController()
{
    m_framePollTimer.stop();
    for (auto &[id, entry] : m_entries)
        retireWorker(entry);
    for (auto &worker : m_retiringWorkers)
        worker->wait();
    for (auto &query : m_queryThreads)
        query->wait();
}

void CameraSourceController::refreshDevices()
{
    if (m_refreshing)
        return;
    // 只清除未使用设备的模式缓存，避免刷新时重新打开正在采集的摄像头。
    for (auto it = m_deviceModes.begin(); it != m_deviceModes.end();) {
        const bool deviceHasActiveWorker =
            std::any_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
                return entry.second.worker && entry.second.source.deviceId() == it.key();
            });
        if (!deviceHasActiveWorker)
            it = m_deviceModes.erase(it);
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
    m_queryThreads.push_back(std::move(query));
}

void CameraSourceController::requestModes(const QString &deviceId)
{
    if (deviceId.isEmpty() || m_pendingModeDeviceIds.contains(deviceId))
        return;
    // 使用缓存可避免查询模式时为同一摄像头额外打开采集会话。
    if (m_deviceModes.contains(deviceId)) {
        emit modesChanged(deviceId, {});
        return;
    }
    m_pendingModeDeviceIds.insert(deviceId);
    auto query = std::unique_ptr<QThread>(QThread::create([this, deviceId] {
        QString error;
        const auto result = cameraModes(deviceId, &error);
        QMetaObject::invokeMethod(
            this,
            [this, deviceId, result, error] {
                m_pendingModeDeviceIds.remove(deviceId);
                if (error.isEmpty())
                    m_deviceModes.insert(deviceId, result);
                emit modesChanged(deviceId, error);
            },
            Qt::QueuedConnection);
    }));
    query->start();
    m_queryThreads.push_back(std::move(query));
}

QString CameraSourceController::addCamera(const QString &name, const QString &deviceId,
                                          const CameraMode &mode)
{
    if (name.trimmed().isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto source = std::make_unique<CameraSource>(id, name.trimmed(), deviceId);
    mode.applyTo(*source);
    if (!source->hasValidCaptureSettings() || !m_sourceManager.addSource(std::move(source)))
        return {};
    return id;
}

bool CameraSourceController::retry(const QString &sourceId)
{
    auto it = m_entries.find(sourceId);
    if (it == m_entries.end())
        return false;
    retireWorker(it->second, false);
    it->second.failed = false;
    it->second.retryAtNs = 0;
    synchronizeSources();
    return true;
}

void CameraSourceController::retireWorker(Entry &entry, bool clearFrame)
{
    if (clearFrame) entry.frame.reset();
    if (entry.worker) {
        entry.worker->stop();
        m_retiringWorkers.push_back(std::move(entry.worker));
    }
}

void CameraSourceController::synchronizeSources()
{
    if (m_updatingSources) {
        if (!m_syncPending) {
            m_syncPending = true;
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_syncPending = false;
                    synchronizeSources();
                },
                Qt::QueuedConnection);
        }
        return;
    }
    m_updatingSources = true;
    const auto clearUpdateGuard = qScopeGuard([this] {
        m_updatingSources = false;
    });
    QSet<QString> retainedSourceIds;
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = dynamic_cast<const CameraSource *>(m_sourceManager.source(id));
        if (!item)
            continue;
        retainedSourceIds.insert(id);
        auto &entry = m_entries[id];
        const auto &requested = *item;
        if (!entry.source.hasSameCaptureSettings(requested)) {
            retireWorker(entry);
            entry.source.copyCaptureSettings(requested);
            entry.failed = false;
            entry.retryAtNs = 0;
        }
        if (!item->visible()) {
            retireWorker(entry);
            entry.failed = false;
            entry.retryAtNs = 0;
            m_sourceManager.setSourceState(id, VideoSourceState::Paused, {});
        } else if (!entry.worker && !entry.failed) {
            // 旧会话尚未退出时不能重新打开同一摄像头，否则驱动可能拒绝访问。
            const bool retiring = std::any_of(
                m_retiringWorkers.cbegin(), m_retiringWorkers.cend(), [&](const auto &worker) {
                    return worker->deviceId() == requested.deviceId() && !worker->isFinished();
                });
            if (retiring) {
                m_sourceManager.setSourceState(id, VideoSourceState::Starting, {});
                continue;
            }
            entry.generation = ++m_generation;
            entry.worker = std::make_unique<CameraCaptureWorker>(requested, entry.generation);
            entry.worker->start();
            m_sourceManager.setSourceState(id, VideoSourceState::Starting, {});
        } else if (entry.failed) {
            m_sourceManager.setSourceState(id, VideoSourceState::Error,
                                           m_sourceManager.sourceError(id));
        }
    }
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (!retainedSourceIds.contains(it->first)) {
            retireWorker(it->second);
            it = m_entries.erase(it);
        } else
            ++it;
    }
    publishRenderSources();
}

void CameraSourceController::collectCaptureUpdates()
{
    if (m_updatingSources)
        return;
    bool captureUpdated = false;
    bool retryDue = false;
    m_updatingSources = true;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker) {
            if (entry.failed && entry.retryAtNs && MediaClock::nowNs() >= entry.retryAtNs) {
                entry.failed = false;
                entry.retryAtNs = 0;
                retryDue = true;
            }
            continue;
        }
        const QString error = entry.worker->lastError();
        if (!error.isEmpty()) {
            retireWorker(entry, false);
            entry.failed = true;
            entry.retryAtNs = MediaClock::nowNs() + 2000000000LL;
            m_sourceManager.setSourceState(id, VideoSourceState::Error, error);
            emit sourceFailed(id, error);
            captureUpdated = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation)
                continue;
            entry.frame = std::move(frame);
            m_sourceManager.updateCameraCapture(
                id, entry.frame->renderFrame->size, entry.frame->frameRateNumerator,
                entry.frame->frameRateDenominator, entry.frame->status);
            captureUpdated = true;
        }
    }
    const bool hadRetiringWorkers = !m_retiringWorkers.empty();
    m_retiringWorkers.erase(std::remove_if(m_retiringWorkers.begin(), m_retiringWorkers.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retiringWorkers.end());
    m_queryThreads.erase(std::remove_if(m_queryThreads.begin(), m_queryThreads.end(),
                                   [](const auto &thread) {
                                       return thread->isFinished();
                                   }),
                    m_queryThreads.end());
    m_updatingSources = false;
    if (hadRetiringWorkers || retryDue)
        synchronizeSources();
    else if (captureUpdated)
        publishRenderSources();
}

void CameraSourceController::publishRenderSources()
{
    QList<RenderSourceSnapshot> result;
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = dynamic_cast<const CameraSource *>(m_sourceManager.source(id));
        if (!item)
            continue;
        RenderSourceSnapshot source;
        source.id = id;
        const auto camera = m_entries.find(id);
        if (camera == m_entries.end())
            continue;
        if (camera->second.frame)
            source.frame = camera->second.frame->renderFrame;
        source.visible = item->visible();
        source.locked = item->locked();
        source.transform = item->transform();
        source.filters = item->filters();
        result.append(std::move(source));
    }
    emit renderSourcesChanged(result);
}

int CameraSourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &[id, entry] : m_entries)
        if (entry.worker && !entry.worker->isFinished())
            ++count;
    for (const auto &worker : m_retiringWorkers)
        if (!worker->isFinished())
            ++count;
    return count;
}

void CameraSourceController::reportCaptureFailure(const QString &sourceId, quint64 generation,
                                          const QString &message)
{
    const auto it = m_entries.find(sourceId);
    if (it == m_entries.end() || it->second.generation != generation || !it->second.worker)
        return;
    auto &entry = it->second;
    retireWorker(entry, false);
    entry.failed = true;
    entry.retryAtNs = MediaClock::nowNs() + 2000000000LL;
    m_sourceManager.setSourceState(sourceId, VideoSourceState::Error, message);
    emit sourceFailed(sourceId, message);
    publishRenderSources();
}
