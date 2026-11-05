#include "video/display/displaysourcecontroller.h"
#include "video/videosourcemanager.h"
#include <QScopeGuard>
#include <QSet>
#include <QUuid>
#include <algorithm>

DisplaySourceController::DisplaySourceController(VideoSourceManager &manager, QObject *parent)
    : QObject(parent), m_sourceManager(manager)
{
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
    for (auto &pending : m_pendingAdds) {
        pending.worker->stop();
        m_retiringWorkers.push_back(std::move(pending.worker));
    }
    for (auto &entry : m_entries)
        retireWorker(entry.second);
    // 仅应用退出时等待；正常移除来源时把线程转入退出队列，避免阻塞界面。
    for (auto &worker : m_retiringWorkers)
        worker->wait();
}

void DisplaySourceController::requestAdd(const QString &name, bool captureCursor, QObject *requester)
{
    cancelAdd(requester);
    if (!requester || name.trimmed().isEmpty()) {
        emit addFailed(requester, tr("请输入来源名称。"));
        return;
    }
    PendingAdd pending{name.trimmed(), captureCursor, requester,
        std::make_unique<DisplayCaptureWorker>(captureCursor, ++m_generation)};
    pending.worker->start();
    m_pendingAdds.push_back(std::move(pending));
}

void DisplaySourceController::cancelAdd(QObject *requester)
{
    for (auto it = m_pendingAdds.begin(); it != m_pendingAdds.end();) {
        if (!it->requester || it->requester == requester) {
            it->worker->stop();
            m_retiringWorkers.push_back(std::move(it->worker));
            it = m_pendingAdds.erase(it);
        } else
            ++it;
    }
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
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = m_sourceManager.source(id);
        if (!item)
            continue;
        if (item->type() != VideoSourceType::DisplayCapture)
            continue;
        const auto *display = static_cast<const DisplaySource *>(item);
        retainedSourceIds.insert(id);
        auto &entry = m_entries[id];
        const bool captureCursor = display->captureCursor();
        if (entry.captureCursor != captureCursor) {
            retireWorker(entry);
            entry.captureCursor = captureCursor;
            entry.failed = false;
        }
        if (!item->visible()) {
            retireWorker(entry);
            m_sourceManager.setSourceState(id, VideoSourceState::Paused,
                                           entry.failed ? m_sourceManager.sourceError(id)
                                                        : QString());
        } else if (!entry.worker && !entry.failed) {
            entry.generation = ++m_generation;
            entry.worker =
                std::make_unique<DisplayCaptureWorker>(captureCursor, entry.generation);
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

bool DisplaySourceController::completePendingAdds()
{
    bool captureUpdated = false;
    // Complete adds only after receiving a real frame. Transfer the authorized
    // worker into the source entry so adding does not open a second picker.
    for (auto it = m_pendingAdds.begin(); it != m_pendingAdds.end();) {
        if (!it->requester) {
            it->worker->stop();
            m_retiringWorkers.push_back(std::move(it->worker));
            it = m_pendingAdds.erase(it);
            continue;
        }
        const auto error = it->worker->lastError();
        auto frame = it->worker->takeLatest();
        if (error.isEmpty() && !frame) {
            ++it;
            continue;
        }
        auto pending = std::move(*it);
        it = m_pendingAdds.erase(it);
        if (!error.isEmpty()) {
            pending.worker->stop();
            m_retiringWorkers.push_back(std::move(pending.worker));
            QMetaObject::invokeMethod(this, [this, requester = pending.requester, error] {
                if (requester)
                    emit addFailed(requester, error);
            }, Qt::QueuedConnection);
            continue;
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!m_sourceManager.addSource(
                std::make_unique<DisplaySource>(id, pending.name, pending.cursor))) {
            pending.worker->stop();
            m_retiringWorkers.push_back(std::move(pending.worker));
            QMetaObject::invokeMethod(this, [this, requester = pending.requester] {
                if (requester)
                    emit addFailed(requester, tr("无法添加显示器来源。"));
            }, Qt::QueuedConnection);
            continue;
        }
        auto &entry = m_entries[id];
        entry.captureCursor = pending.cursor;
        entry.generation = pending.worker->generation();
        entry.worker = std::move(pending.worker);
        entry.frame = std::move(frame);
        m_sourceManager.updateSourceStatus(id, entry.frame->size, VideoSourceState::Capturing);
        captureUpdated = true;
        QMetaObject::invokeMethod(this, [this, requester = pending.requester] {
            if (requester)
                emit displayAdded(requester);
        }, Qt::QueuedConnection);
    }
    return captureUpdated;
}

void DisplaySourceController::collectCaptureUpdates()
{
    if (m_updatingSources)
        return;
    m_updatingSources = true;
    const auto clearUpdateGuard = qScopeGuard([this] {
        m_updatingSources = false;
    });
    bool captureUpdated = completePendingAdds();
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker)
            continue;
        const QString error = entry.worker->lastError();
        if (!error.isEmpty()) {
            entry.failed = true;
            retireWorker(entry);
            m_sourceManager.setSourceState(id, VideoSourceState::Error, error);
            emit sourceFailed(id, error);
            captureUpdated = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation)
                continue;
            entry.frame = std::move(frame);
            m_sourceManager.updateSourceStatus(id, entry.frame->size, VideoSourceState::Capturing);
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
    const auto sourceIds = m_sourceManager.sourceIds();
    for (const auto &id : sourceIds) {
        const auto *item = m_sourceManager.source(id);
        if (!item)
            continue;
        if (item->type() != VideoSourceType::DisplayCapture)
            continue;
        RenderSourceSnapshot source;
        source.id = id;
        const auto captureEntry = m_entries.find(id);
        if (captureEntry == m_entries.end())
            continue;
        source.frame = captureEntry->second.frame;
        source.visible = item->visible();
        source.locked = item->locked();
        source.transform = item->transform();
        source.filters = item->filters();
        result.append(std::move(source));
    }
    emit renderSourcesChanged(result);
}

int DisplaySourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &pending : m_pendingAdds)
        if (!pending.worker->isFinished())
            ++count;
    for (const auto &entry : m_entries)
        if (entry.second.worker && !entry.second.worker->isFinished())
            ++count;
    for (const auto &worker : m_retiringWorkers)
        if (!worker->isFinished())
            ++count;
    return count;
}
