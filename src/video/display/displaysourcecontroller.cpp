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
    const auto sync = [this] {
        synchronize();
    };
    connect(&manager, &VideoSourceManager::sourcesChanged, this, sync);
    connect(&manager, &VideoSourceManager::sourceChanged, this, sync);
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(33);
    connect(&m_timer, &QTimer::timeout, this, &DisplaySourceController::poll);
    m_timer.start();
    QMetaObject::invokeMethod(this, sync, Qt::QueuedConnection);
}

DisplaySourceController::~DisplaySourceController()
{
    m_timer.stop();
    for (auto &entry : m_entries)
        stop(entry.second);
    // App shutdown only. Normal deletion retires workers without a GUI-thread join.
    for (auto &worker : m_retired)
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
                                            bool cursor)
{
    if (name.trimmed().isEmpty() || displayId.trimmed().isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    // Keep the source even if opening the selected display subsequently fails.
    if (!m_sourceManager.addSource(
            {id, name.trimmed(), DisplaySourceConfig{displayId, cursor}}))
        return {};
    return id;
}

bool DisplaySourceController::retry(const QString &sourceId)
{
    auto it = m_entries.find(sourceId);
    if (it == m_entries.end())
        return false;
    stop(it->second);
    it->second.failed = false;
    synchronize();
    return true;
}

void DisplaySourceController::stop(Entry &entry)
{
    entry.frame.reset();
    if (!entry.worker)
        return;
    entry.worker->stop();
    m_retired.push_back(std::move(entry.worker));
}

void DisplaySourceController::synchronize()
{
    if (m_busy) {
        if (!m_syncPending) {
            m_syncPending = true;
            QMetaObject::invokeMethod(
                this,
                [this] {
                    m_syncPending = false;
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
        if (item.type() != VideoSourceType::DisplayCapture)
            continue;
        const QString id = item.id();
        const auto &config = std::get<DisplaySourceConfig>(item.config());
        retained.insert(id);
        auto &entry = m_entries[id];
        const QString displayId = config.displayId();
        const bool cursor = config.captureCursor();
        if (entry.displayId != displayId || entry.cursor != cursor) {
            stop(entry);
            entry.displayId = displayId;
            entry.cursor = cursor;
            entry.failed = false;
        }
        if (!item.visible()) {
            stop(entry);
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Paused,
                             entry.failed ? m_sourceManager.sourceRuntime(id).error() : QString());
        } else if (!entry.worker && !entry.failed) {
            entry.generation = ++m_generation;
            entry.worker =
                std::make_unique<DisplayCaptureWorker>(displayId, cursor, entry.generation);
            entry.worker->start();
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Starting, {});
        } else if (entry.failed) {
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Error,
                             m_sourceManager.sourceRuntime(id).error());
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

void DisplaySourceController::failTransfer(const QString &sourceId, quint64 generation,
                                           const QString &reason)
{
    const auto it = m_entries.find(sourceId);
    if (it == m_entries.end() || it->second.generation != generation || !it->second.worker ||
        it->second.failed)
        return;
    auto &entry = it->second;
    entry.failed = true;
    stop(entry);
    const QString message = tr("GPU 画面传输失败，已停止采集。请检查显卡驱动后重新添加来源。");
    setRuntimeStatus(m_sourceManager, sourceId, VideoSourceState::Error, message);
    qWarning().noquote() << "Display GPU transfer failed:" << sourceId << reason;
    emit sourceFailed(sourceId, message);
    publish();
}

void DisplaySourceController::poll()
{
    if (m_busy)
        return;
    m_busy = true;
    const auto reset = qScopeGuard([this] {
        m_busy = false;
    });
    bool changed = false;
    for (auto &[id, entry] : m_entries) {
        if (!entry.worker)
            continue;
        const QString error = entry.worker->lastError();
        if (!error.isEmpty()) {
            entry.failed = true;
            stop(entry);
            setRuntimeStatus(m_sourceManager, id, VideoSourceState::Error, error);
            emit sourceFailed(id, error);
            changed = true;
        } else if (auto frame = entry.worker->takeLatest()) {
            if (frame->generation != entry.generation)
                continue;
            entry.frame = std::move(frame);
            auto runtime = m_sourceManager.sourceRuntime(id);
            runtime.setPixelSize(entry.frame->size);
            runtime.setState(VideoSourceState::Capturing);
            runtime.setError({});
            m_sourceManager.setSourceRuntime(id, runtime);
            changed = true;
        }
    }
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retired.end());
    if (changed)
        publish();
}

void DisplaySourceController::publish()
{
    QList<PreviewSource> result;
    const auto sourceEntries = m_sourceManager.sources();
    for (const auto &item : sourceEntries) {
        if (item.type() != VideoSourceType::DisplayCapture)
            continue;
        const QString id = item.id();
        PreviewSource source;
        source.id = id;
        const auto dynamic = m_entries.find(id);
        if (dynamic == m_entries.end())
            continue;
        source.frame = dynamic->second.frame;
        source.visible = item.visible();
        source.locked = item.locked();
        source.transform = item.transform();
        source.filters = item.filters();
        result.append(std::move(source));
    }
    emit sourcesChanged(result);
}

int DisplaySourceController::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries)
        if (entry.second.worker && !entry.second.worker->isFinished())
            ++count;
    for (const auto &worker : m_retired)
        if (!worker->isFinished())
            ++count;
    return count;
}
