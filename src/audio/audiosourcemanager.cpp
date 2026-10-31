#include "audio/audiosourcemanager.h"
#include <QUuid>

AudioSourceManager::AudioSourceManager(QObject *parent) : QObject(parent)
{
    m_mixer.start();
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &AudioSourceManager::poll);
    m_timer.start();
}

AudioSourceManager::~AudioSourceManager()
{
    m_timer.stop();
    for (auto &entry : m_entries)
        entry.worker->stop();
    for (auto &worker : m_retired)
        worker->stop();
    m_mixer.stop();
    m_mixer.wait();
    for (auto &entry : m_entries)
        entry.worker->wait();
    for (auto &worker : m_retired)
        worker->wait();
    for (auto &query : m_queries)
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

const AudioSource *AudioSourceManager::source(const QString &id) const
{
    const int row = rowFor(id);
    return row >= 0 ? &m_entries[size_t(row)].source : nullptr;
}

int AudioSourceManager::rowFor(const QString &id) const
{
    for (int row = 0; row < count(); ++row)
        if (m_entries[size_t(row)].source.id == id)
            return row;
    return -1;
}

bool AudioSourceManager::nameAvailable(const QString &name, const QString &except) const
{
    if (name.isEmpty() || name.size() > 80)
        return false;
    return std::none_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
        return entry.source.id != except &&
               entry.source.name.compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString AudioSourceManager::suggestedSourceName(const QString &base) const
{
    const QString name = base.trimmed().left(65);
    if (name.isEmpty())
        return {};
    if (nameAvailable(name))
        return name;
    for (int count = 2; count < 100000; ++count) {
        const auto candidate = QStringLiteral("%1 %2").arg(name).arg(count);
        if (nameAvailable(candidate))
            return candidate;
    }
    return {};
}

bool AudioSourceManager::deviceInUse(const QString &id) const
{
    // Includes retiring workers: do not reopen an endpoint before its old client closes.
    return std::any_of(m_entries.cbegin(), m_entries.cend(),
                       [&](const auto &entry) {
                           return entry.source.deviceId.compare(id, Qt::CaseInsensitive) == 0;
                       }) ||
           std::any_of(m_retired.cbegin(), m_retired.cend(), [&](const auto &worker) {
               return worker->deviceId().compare(id, Qt::CaseInsensitive) == 0;
           });
}

QString AudioSourceManager::addSource(const QString &name, const QString &type,
                                      const QString &deviceId)
{
    if (!nameAvailable(name.trimmed()) || (type != "microphone" && type != "system") ||
        deviceId.isEmpty() || deviceId.size() > 2048 || deviceInUse(deviceId))
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
    synchronize();
    emit sourcesChanged();
    if (rowFor(id) >= 0)
        selectSource(id);
    emit devicesChanged();
    return id;
}

bool AudioSourceManager::removeSource(const QString &id)
{
    const int row = rowFor(id);
    if (row < 0)
        return false;
    auto &entry = m_entries[size_t(row)];
    entry.worker->stop();
    entry.queue->clear();
    m_retired.push_back(std::move(entry.worker));
    m_entries.erase(m_entries.begin() + row);
    synchronize();
    if (m_selected == id)
        selectSource(count() ? m_entries[size_t(std::min(row, count() - 1))].source.id : QString());
    emit sourcesChanged();
    emit devicesChanged();
    return true;
}

bool AudioSourceManager::renameSource(const QString &id, const QString &name)
{
    const int row = rowFor(id);
    if (row < 0 || !nameAvailable(name.trimmed(), id))
        return false;
    if (m_entries[size_t(row)].source.name == name.trimmed())
        return true;
    m_entries[size_t(row)].source.name = name.trimmed();
    emit sourceChanged(id);
    return true;
}

bool AudioSourceManager::setVolume(const QString &id, qreal volume)
{
    const int row = rowFor(id);
    if (row < 0 || !std::isfinite(volume) || volume < 0 || volume > 1)
        return false;
    if (m_entries[size_t(row)].source.volume == float(volume))
        return true;
    m_entries[size_t(row)].source.volume = float(volume);
    synchronize();
    emit sourceChanged(id);
    return true;
}

bool AudioSourceManager::setMuted(const QString &id, bool muted)
{
    const int row = rowFor(id);
    if (row < 0)
        return false;
    if (m_entries[size_t(row)].source.muted == muted)
        return true;
    m_entries[size_t(row)].source.muted = muted;
    synchronize();
    emit sourceChanged(id);
    return true;
}

bool AudioSourceManager::selectSource(const QString &id)
{
    if (!id.isEmpty() && rowFor(id) < 0)
        return false;
    if (m_selected != id) {
        m_selected = id;
        emit currentSourceIdChanged();
    }
    return true;
}

void AudioSourceManager::synchronize()
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
    m_queries.push_back(std::move(query));
}

void AudioSourceManager::poll()
{
    const auto levels = m_mixer.takeLevels();
    QStringList ids;
    for (const auto &entry : m_entries)
        ids.append(entry.source.id);
    for (const auto &id : ids) {
        const int row = rowFor(id);
        if (row < 0)
            continue;
        auto &entry = m_entries[size_t(row)];
        const auto state = entry.worker->status();
        const bool statusChanged =
            state.state != entry.source.state || state.error != entry.source.error;
        entry.source.state = state.state;
        entry.source.error = state.error;
        const auto level = levels.sources.value(id);
        const AudioPeak peak{std::max(level.left, entry.source.peak.left * .72f),
                             std::max(level.right, entry.source.peak.right * .72f)};
        const bool levelsChanged =
            peak.left != entry.source.peak.left || peak.right != entry.source.peak.right;
        entry.source.peak = peak;

        // Notifications may synchronously remove sources or reallocate the entries.
        if (statusChanged)
            emit sourceChanged(id);
        if (levelsChanged && rowFor(id) >= 0)
            emit sourceLevelsChanged(id);
        if (statusChanged && state.state == "error" && rowFor(id) >= 0)
            emit sourceError(id, state.error);
    }
    m_master = {std::max(levels.master.left, m_master.left * .72f),
                std::max(levels.master.right, m_master.right * .72f)};
    if (levels.clipped)
        m_clipTicks = 25;
    else if (m_clipTicks)
        --m_clipTicks;
    emit masterLevelsChanged();
    const auto retired = m_retired.size();
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(),
                                   [](const auto &worker) {
                                       return worker->isFinished();
                                   }),
                    m_retired.end());
    if (retired != m_retired.size())
        emit devicesChanged();
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(),
                                   [](const auto &query) {
                                       return query->isFinished();
                                   }),
                    m_queries.end());
}

int AudioSourceManager::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries)
        if (entry.worker->isRunning())
            ++count;
    for (const auto &worker : m_retired)
        if (worker->isRunning())
            ++count;
    return count;
}
