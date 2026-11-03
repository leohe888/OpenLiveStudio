#include "video/display/displaysourcecontroller.h"
#include "video/display/displaydevices.h"
#include "video/videosourcemanager.h"
#include <QDebug>
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace {
void setRuntimeStatus(VideoSourceManager &manager, const QString &sourceId,
                      VideoSourceState state, const QString &error)
{
    auto runtime = manager.sourceRuntime(sourceId);
    runtime.setState(state);
    runtime.setError(error);
    manager.setSourceRuntime(sourceId, runtime);
}
}

DisplaySourceController::DisplaySourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
    refreshDisplayDevices();
    const auto synchronizeSourcesFromManager = [this] {
        synchronizeSources();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, synchronizeSourcesFromManager);
    connect(&manager, &VideoSourceManager::sourceChanged, this, synchronizeSourcesFromManager);
    m_framePollTimer.setTimerType(Qt::PreciseTimer);
    m_framePollTimer.setInterval(33);
    connect(&m_framePollTimer, &QTimer::timeout, this,
            &DisplaySourceController::collectCaptureUpdates);
    m_framePollTimer.start();
    QMetaObject::invokeMethod(this, synchronizeSourcesFromManager, Qt::QueuedConnection);
}

DisplaySourceController::~DisplaySourceController()
{
    m_framePollTimer.stop();
    for (auto &entry : m_entries)
        retireWorker(entry.second);
    // 仅应用退出时等待；正常移除来源时把线程转入退出队列，避免阻塞界面。
    for (auto &worker : m_retiringWorkers)
        worker->wait();
}

void DisplaySourceController::refreshDisplayDevices()
{
    const auto devices = ::displayDevices();
    if (devices != m_devices) {
        m_devices = devices;
        emit displayDevicesChanged();
    }
}

QString DisplaySourceController::addDisplay(const QString &name, const QString &displayId,
                                            bool captureCursor)
{
    if (name.trimmed().isEmpty() || displayId.trimmed().isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // 即使后续打开设备失败，也保留来源配置，方便用户查看错误或重试。
    if (!m_sourceManager.addSource(
            {id, name.trimmed(), DisplaySourceConfig{displayId, captureCursor}}))
        return {};
    return id;
}

bool DisplaySourceController::retry(const QString &sourceId)
{
    auto it = m_entries.find(sourceId);
    if (it == m_entries.end())
        return false;
    retireWorker(it->second);
    it->second.failed = false;
    synchronizeSources();
    return true;
}

void DisplaySourceController::retireWorker(Entry &entry)
{
    entry.frame.reset();
    if (!entry.worker)
        return;
    entry.worker->stop();
    m_retiringWorkers.push_back(std::move(entry.worker));
}

void DisplaySourceController::synchronizeSources()
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
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        if (item.type() != VideoSourceType::DisplayCapture)
            continue;
        const QString id = item.id();
        const auto &config = std::get<DisplaySourceConfig>(item.config());
        retainedSourceIds.insert(id);
        auto &entry = m_entries[id];
        const QString displayId = config.displayId();
        const bool captureCursor = config.captureCursor();
        if (entry.displayId != displayId || entry.captureCursor != captureCursor) {
            retireWorker(entry);
            entry.displayId = displayId;
            entry.captureCursor = captureCursor;
            entry.failed = false;
        }
        if (!item.visible()) {
            retireWorker(entry);
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Paused,
                             entry.failed ? m_sourceManager.sourceRuntime(id).error() : QString());
        } else if (!entry.worker && !entry.failed) {
            entry.generation = ++m_generation;
            entry.worker =
                std::make_unique<DisplayCaptureWorker>(displayId, captureCursor, entry.generation);
            entry.worker->start();
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Starting, {});
        } else if (entry.failed) {
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Error,
                             m_sourceManager.sourceRuntime(id).error());
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

void DisplaySourceController::failTransfer(const QString &sourceId, quint64 generation,
                                           const QString &reason)
{
    const auto it = m_entries.find(sourceId);
    if (it == m_entries.end() || it->second.generation != generation || !it->second.worker ||
        it->second.failed)
        return;
    auto &entry = it->second;
    entry.failed = true;
    retireWorker(entry);
    const QString message = tr("GPU 画面传输失败，已停止采集。请检查显卡驱动后重新添加来源。");
    setRuntimeStatus(m_sourceManager, sourceId, VideoSourceState::Error, message);
    qWarning().noquote() << "Display GPU transfer failed:" << sourceId << reason;
    emit sourceFailed(sourceId, message);
    publishRenderSources();
}

void DisplaySourceController::collectCaptureUpdates()
{
    if (m_updatingSources)
        return;
    m_updatingSources = true;
    const auto clearUpdateGuard = qScopeGuard([this] {
        m_updatingSources = false;
    });
    bool captureUpdated = false;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker)
            continue;
        const QString error = entry.worker->lastError();
        if (!error.isEmpty()) {
            entry.failed = true;
            retireWorker(entry);
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Error, error);
            emit sourceFailed(id, error);
            captureUpdated = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation)
                continue;
            entry.frame = std::move(frame);
            auto runtime = m_sourceManager.sourceRuntime(id);
            runtime.setPixelSize(entry.frame->size);
            runtime.setState(VideoSourceState::Capturing);
            runtime.setError({});
            m_sourceManager.setSourceRuntime(id, runtime);
            captureUpdated = true;
        }
    }
    m_retiringWorkers.erase(std::remove_if(m_retiringWorkers.begin(), m_retiringWorkers.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retiringWorkers.end());
    if (captureUpdated)
        publishRenderSources();
}

void DisplaySourceController::publishRenderSources()
{
    QList<RenderSourceSnapshot> result;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        if (item.type() != VideoSourceType::DisplayCapture)
            continue;
        const QString id = item.id();
        RenderSourceSnapshot source;
        source.id = id;
        const auto captureEntry = m_entries.find(id);
        if (captureEntry == m_entries.end())
            continue;
        source.frame = captureEntry->second.frame;
        source.visible = item.visible();
        source.locked = item.locked();
        source.transform = item.transform();
        source.filters = item.filters();
        result.append(std::move(source));
    }
    emit renderSourcesChanged(result);
}

int DisplaySourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries)
        if (entry.second.worker && !entry.second.worker->isFinished())
            ++count;
    for (const auto &worker : m_retiringWorkers)
        if (!worker->isFinished())
            ++count;
    return count;
}
