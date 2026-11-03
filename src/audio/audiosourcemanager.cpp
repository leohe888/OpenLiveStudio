#include "audio/audiosourcemanager.h"
#include <QUuid>

AudioSourceManager::AudioSourceManager(QObject *parent) : QObject(parent)
{
    m_mixer.start();
    m_statusPollTimer.setInterval(40);
    connect(&m_statusPollTimer, &QTimer::timeout, this,
            &AudioSourceManager::refreshSourceStatusAndLevels);
    m_statusPollTimer.start();
}

AudioSourceManager::~AudioSourceManager()
{
    m_statusPollTimer.stop();
    for (auto &entry : m_entries)
        entry.worker->stop();
    for (auto &worker : m_retiringWorkers)
        worker->stop();
    m_mixer.stop();
    m_mixer.wait();
    for (auto &entry : m_entries)
        entry.worker->wait();
    for (auto &worker : m_retiringWorkers)
        worker->wait();
    for (auto &query : m_deviceQueryThreads)
        query->wait();
}

QList<AudioSource> AudioSourceManager::sources() const
{
    QList<AudioSource> result;
    result.reserve(count());
    for (const auto &entry : m_entries)
        result.append(entry.source);
    return result;
}

const AudioSource *AudioSourceManager::source(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? &m_entries[size_t(row)].source : nullptr;
}

int AudioSourceManager::sourceRow(const QString &sourceId) const
{
    for (int row = 0; row < count(); ++row)
        if (m_entries[size_t(row)].source.id == sourceId)
            return row;
    return -1;
}

bool AudioSourceManager::isSourceNameAvailable(const QString &name,
                                               const QString &excludedSourceId) const
{
    if (name.isEmpty() || name.size() > 80)
        return false;
    return std::none_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
        return entry.source.id != excludedSourceId &&
               entry.source.name.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString AudioSourceManager::suggestedSourceName(const QString &baseName) const
{
    const QString name = baseName.trimmed().left(65);
    if (name.isEmpty())
        return {};
    if (isSourceNameAvailable(name))
        return name;
    for (int count = 2; count < 100000; ++count) {
        const auto candidate = QStringLiteral("%1 %2").arg(name).arg(count);
        if (isSourceNameAvailable(candidate))
            return candidate;
    }
    return {};
}

bool AudioSourceManager::deviceInUse(const QString &deviceId) const
{
    // Includes retiring workers: do not reopen an endpoint before its old client closes.
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&](const auto &entry) {
                           return entry.source.deviceId.compare(deviceId, Qt::CaseInsensitive) == 0;
                       }) ||
           std::any_of(m_retiringWorkers.cbegin(), m_retiringWorkers.cend(),
                       [&](const auto &worker) {
                           return worker->deviceId().compare(deviceId, Qt::CaseInsensitive) == 0;
                       });
}

QString AudioSourceManager::addSource(const QString &name, AudioSourceType type,
                                      const QString &deviceId)
{
    if (!isSourceNameAvailable(name.trimmed()) || deviceId.isEmpty() || deviceId.size() > 2048 ||
        deviceInUse(deviceId))
        return {};
    Entry entry;
    entry.source.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.source.name = name.trimmed();
    entry.source.type = type;
    entry.source.deviceId = deviceId;
    entry.source.deviceName = deviceId;
    for (const auto &device : m_devices) {
        if (device.id == deviceId) {
            if (device.type != type)
                return {};
            entry.source.deviceName = device.name;
            break;
        }
    }
    entry.queue = std::make_shared<AudioPacketQueue>();
    entry.worker = std::make_unique<AudioCaptureWorker>(deviceId, type, entry.queue);
    const QString id = entry.source.id;
    m_entries.push_back(std::move(entry));
    m_entries.back().worker->start();
    updateMixerInputs();
    emit sourcesChanged();
    if (sourceRow(id) >= 0)
        selectSource(id);
    emit devicesChanged();
    return id;
}

bool AudioSourceManager::removeSource(const QString &sourceId)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &entry = m_entries[size_t(row)];
    entry.worker->stop();
    entry.queue->clear();
    m_retiringWorkers.push_back(std::move(entry.worker));
    m_entries.erase(m_entries.begin() + row);
    updateMixerInputs();
    if (m_currentSourceId == sourceId)
        selectSource(count() ? m_entries[size_t(std::min(row, count() - 1))].source.id : QString());
    emit sourcesChanged();
    emit devicesChanged();
    return true;
}

bool AudioSourceManager::renameSource(const QString &sourceId, const QString &name)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !isSourceNameAvailable(name.trimmed(), sourceId))
        return false;
    if (m_entries[size_t(row)].source.name == name.trimmed())
        return true;
    m_entries[size_t(row)].source.name = name.trimmed();
    emit sourceChanged(sourceId);
    return true;
}

bool AudioSourceManager::setVolume(const QString &sourceId, qreal volume)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !std::isfinite(volume) || volume < 0 || volume > 1)
        return false;
    if (m_entries[size_t(row)].source.volume == float(volume))
        return true;
    m_entries[size_t(row)].source.volume = float(volume);
    updateMixerInputs();
    emit sourceChanged(sourceId);
    return true;
}

bool AudioSourceManager::setMuted(const QString &sourceId, bool muted)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_entries[size_t(row)].source.muted == muted)
        return true;
    m_entries[size_t(row)].source.muted = muted;
    updateMixerInputs();
    emit sourceChanged(sourceId);
    return true;
}

bool AudioSourceManager::selectSource(const QString &sourceId)
{
    if (!sourceId.isEmpty() && sourceRow(sourceId) < 0)
        return false;
    if (m_currentSourceId != sourceId) {
        m_currentSourceId = sourceId;
        emit currentSourceIdChanged();
    }
    return true;
}

void AudioSourceManager::updateMixerInputs()
{
    QList<AudioInput> inputs;
    for (const auto &entry : m_entries)
        inputs.append({entry.source.id, entry.source.volume, entry.source.muted, entry.queue});
    m_mixer.setInputs(inputs);
}

void AudioSourceManager::refreshDevices()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    m_catalogError.clear();
    emit devicesChanged();
    auto query = std::unique_ptr<QThread>(QThread::create([this] {
        QString error;
        const auto devices = audioDevices(&error);
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
    m_deviceQueryThreads.push_back(std::move(query));
}

void AudioSourceManager::refreshSourceStatusAndLevels()
{
    const auto levels = m_mixer.takeLevels();
    QStringList ids;
    for (const auto &entry : m_entries)
        ids.append(entry.source.id);
    for (const auto &id : ids) {
        const int row = sourceRow(id);
        if (row < 0)
            continue;
        auto &entry = m_entries[size_t(row)];
        const auto workerStatus = entry.worker->status();
        const bool statusChanged =
            workerStatus.state != entry.source.state || workerStatus.error != entry.source.error;
        entry.source.state = workerStatus.state;
        entry.source.error = workerStatus.error;
        const auto level = levels.sources.value(id);
        const AudioPeak peak{std::max(level.left, entry.source.peak.left * .72f),
                             std::max(level.right, entry.source.peak.right * .72f)};
        const bool levelsChanged =
            peak.left != entry.source.peak.left || peak.right != entry.source.peak.right;
        entry.source.peak = peak;

        // Notifications may synchronously remove sources or reallocate the entries.
        if (statusChanged)
            emit sourceChanged(id);
        if (levelsChanged && sourceRow(id) >= 0)
            emit sourceLevelsChanged(id);
        if (statusChanged && workerStatus.state == "error" && sourceRow(id) >= 0)
            emit sourceFailed(id, workerStatus.error);
    }
    m_masterPeak = {std::max(levels.master.left, m_masterPeak.left * .72f),
                std::max(levels.master.right, m_masterPeak.right * .72f)};
    if (levels.clipped)
        m_clippingHoldTicks = 25;
    else if (m_clippingHoldTicks)
        --m_clippingHoldTicks;
    emit masterLevelsChanged();
    const auto retiringWorkerCountBeforeCleanup = m_retiringWorkers.size();
    m_retiringWorkers.erase(std::remove_if(m_retiringWorkers.begin(), m_retiringWorkers.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retiringWorkers.end());
    if (retiringWorkerCountBeforeCleanup != m_retiringWorkers.size())
        emit devicesChanged();
    m_deviceQueryThreads.erase(std::remove_if(m_deviceQueryThreads.begin(),
                                              m_deviceQueryThreads.end(),
                                              [](const auto &query) {
                                                  return query->isFinished();
                                              }),
                               m_deviceQueryThreads.end());
}

int AudioSourceManager::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries)
        if (entry.worker->isRunning())
            ++count;
    for (const auto &worker : m_retiringWorkers)
        if (worker->isRunning())
            ++count;
    return count;
}
