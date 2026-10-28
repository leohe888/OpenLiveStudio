#include "audiosourcemodel.h"
#include <QUuid>

AudioSourceModel::AudioSourceModel(QObject *parent) : QAbstractListModel(parent)
{
    m_mixer.start();
    m_timer.setInterval(40);
    connect(&m_timer, &QTimer::timeout, this, &AudioSourceModel::poll);
    m_timer.start();
}
AudioSourceModel::~AudioSourceModel()
{
    m_timer.stop();
    for (auto &entry : m_entries) entry.worker->stop();
    for (auto &worker : m_retired) worker->stop();
    m_mixer.stop(); m_mixer.wait();
    for (auto &entry : m_entries) entry.worker->wait();
    for (auto &worker : m_retired) worker->wait();
    for (auto &query : m_queries) query->wait();
}
int AudioSourceModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_entries.size()); }
QVariant AudioSourceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto &entry = m_entries[size_t(index.row())];
    switch (role) {
    case IdRole: return entry.id;
    case Qt::DisplayRole:
    case NameRole: return entry.name;
    case TypeRole: return entry.type;
    case DeviceIdRole: return entry.deviceId;
    case DeviceNameRole: return entry.deviceName;
    case VolumeRole: return entry.volume;
    case MutedRole: return entry.muted;
    case StateRole: return entry.status.state;
    case ErrorRole: return entry.status.error;
    case PeakLeftRole: return entry.peak.left;
    case PeakRightRole: return entry.peak.right;
    default: return {};
    }
}
QHash<int, QByteArray> AudioSourceModel::roleNames() const
{
    return {{IdRole,"soundId"}, {NameRole,"soundName"}, {TypeRole,"soundType"}, {DeviceIdRole,"soundDeviceId"},
        {DeviceNameRole,"soundDeviceName"}, {VolumeRole,"soundVolume"}, {MutedRole,"soundMuted"},
        {StateRole,"soundState"}, {ErrorRole,"soundError"}, {PeakLeftRole,"soundPeakLeft"}, {PeakRightRole,"soundPeakRight"}};
}
int AudioSourceModel::rowFor(const QString &id) const
{
    for (int row = 0; row < rowCount(); ++row) if (m_entries[size_t(row)].id == id) return row;
    return -1;
}
bool AudioSourceModel::nameAvailable(const QString &name, const QString &except) const
{
    if (name.isEmpty() || name.size() > 80) return false;
    return std::none_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) {
        return entry.id != except && entry.name.compare(name, Qt::CaseInsensitive) == 0;
    });
}
QString AudioSourceModel::suggestedSourceName(const QString &base) const
{
    const QString name = base.trimmed().left(65);
    if (name.isEmpty()) return {};
    if (nameAvailable(name)) return name;
    for (int count = 2; count < 100000; ++count) {
        const auto candidate = QStringLiteral("%1 %2").arg(name).arg(count);
        if (nameAvailable(candidate)) return candidate;
    }
    return {};
}
bool AudioSourceModel::deviceInUse(const QString &id) const
{
    // Includes retiring workers: do not reopen an endpoint before its old client closes.
    return std::any_of(m_entries.cbegin(), m_entries.cend(), [&](const auto &entry) { return entry.deviceId.compare(id, Qt::CaseInsensitive) == 0; })
        || std::any_of(m_retired.cbegin(), m_retired.cend(), [&](const auto &worker) { return worker->deviceId().compare(id, Qt::CaseInsensitive) == 0; });
}
QString AudioSourceModel::addSource(const QString &name, const QString &type, const QString &deviceId)
{
    if (!nameAvailable(name.trimmed()) || (type != "microphone" && type != "system")
        || deviceId.isEmpty() || deviceId.size() > 2048 || deviceInUse(deviceId)) return {};
    Entry entry;
    entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.name = name.trimmed(); entry.type = type; entry.deviceId = deviceId; entry.deviceName = deviceId;
    for (const auto &device : m_devices) {
        const auto info = device.toMap();
        if (info.value("id").toString() == deviceId) {
            if (info.value("type").toString() != type) return {};
            entry.deviceName = info.value("name").toString(); break;
        }
    }
    entry.queue = std::make_shared<AudioPacketQueue>();
    entry.worker = std::make_unique<AudioCaptureWorker>(deviceId, type, entry.queue);
    const QString id = entry.id;
    beginInsertRows({}, rowCount(), rowCount());
    m_entries.push_back(std::move(entry));
    m_entries.back().worker->start();
    endInsertRows(); synchronize();
    if (rowFor(id) >= 0) selectSource(id);
    emit countChanged(); emit devicesChanged();
    return id;
}
bool AudioSourceModel::removeSource(const QString &id)
{
    const int row = rowFor(id); if (row < 0) return false;
    beginRemoveRows({}, row, row);
    auto &entry = m_entries[size_t(row)];
    entry.worker->stop(); entry.queue->clear();
    m_retired.push_back(std::move(entry.worker));
    m_entries.erase(m_entries.begin() + row); endRemoveRows();
    synchronize();
    if (m_selected == id) selectSource(rowCount() ? m_entries[size_t(std::min(row, rowCount() - 1))].id : QString());
    emit countChanged(); emit devicesChanged(); return true;
}
bool AudioSourceModel::renameSource(const QString &id, const QString &name)
{
    const int row = rowFor(id); if (row < 0 || !nameAvailable(name.trimmed(), id)) return false;
    m_entries[size_t(row)].name = name.trimmed();
    emit dataChanged(index(row), index(row), {NameRole}); return true;
}
bool AudioSourceModel::setVolume(const QString &id, qreal volume)
{
    const int row = rowFor(id); if (row < 0 || !std::isfinite(volume) || volume < 0 || volume > 1) return false;
    m_entries[size_t(row)].volume = float(volume); synchronize();
    emit dataChanged(index(row), index(row), {VolumeRole}); return true;
}
bool AudioSourceModel::setMuted(const QString &id, bool muted)
{
    const int row = rowFor(id); if (row < 0) return false;
    m_entries[size_t(row)].muted = muted; synchronize();
    emit dataChanged(index(row), index(row), {MutedRole}); return true;
}
bool AudioSourceModel::selectSource(const QString &id)
{
    if (!id.isEmpty() && rowFor(id) < 0) return false;
    if (m_selected != id) { m_selected = id; emit currentSourceIdChanged(); }
    return true;
}
QVariantMap AudioSourceModel::source(const QString &id) const
{
    const int row = rowFor(id); if (row < 0) return {};
    const auto &entry = m_entries[size_t(row)];
    return {{"id",entry.id}, {"name",entry.name}, {"type",entry.type}, {"deviceId",entry.deviceId},
        {"deviceName",entry.deviceName}, {"volume",entry.volume}, {"muted",entry.muted},
        {"state",entry.status.state}, {"error",entry.status.error}};
}
void AudioSourceModel::synchronize()
{
    QList<AudioInput> inputs;
    for (const auto &entry : m_entries) inputs.append({entry.id, entry.volume, entry.muted, entry.queue});
    m_mixer.setInputs(inputs);
}
void AudioSourceModel::refreshDevices()
{
    if (m_refreshing) return;
    m_refreshing = true; m_catalogError.clear(); emit devicesChanged();
    auto query = std::unique_ptr<QThread>(QThread::create([this] {
        QString error;
        const auto devices = audioDevices(&error);
        QMetaObject::invokeMethod(this, [this, devices, error] {
            m_devices = devices; m_catalogError = error; m_refreshing = false; emit devicesChanged();
        }, Qt::QueuedConnection);
    }));
    query->start(); m_queries.push_back(std::move(query));
}
void AudioSourceModel::poll()
{
    const auto levels = m_mixer.takeLevels();
    QStringList ids;
    for (const auto &entry : m_entries) ids.append(entry.id);
    for (const auto &id : ids) {
        const int row = rowFor(id);
        if (row < 0) continue;
        auto &entry = m_entries[size_t(row)];
        const auto state = entry.worker->status();
        const bool notify = state.state != entry.status.state || state.error != entry.status.error;
        entry.status = state;
        const auto level = levels.sources.value(entry.id);
        entry.peak = {std::max(level.left, entry.peak.left * .72f), std::max(level.right, entry.peak.right * .72f)};
        emit dataChanged(index(row), index(row), {StateRole, ErrorRole, PeakLeftRole, PeakRightRole});
        // Both notifications can synchronously remove/reallocate model entries.
        if (notify && state.state == "error" && rowFor(id) >= 0) emit sourceError(id, state.error);
    }
    m_master = {std::max(levels.master.left, m_master.left * .72f), std::max(levels.master.right, m_master.right * .72f)};
    if (levels.clipped) m_clipTicks = 25; else if (m_clipTicks) --m_clipTicks;
    emit masterLevelsChanged();
    const auto retired = m_retired.size();
    m_retired.erase(std::remove_if(m_retired.begin(), m_retired.end(), [](const auto &worker) { return worker->isFinished(); }), m_retired.end());
    if (retired != m_retired.size()) emit devicesChanged();
    m_queries.erase(std::remove_if(m_queries.begin(), m_queries.end(), [](const auto &query) { return query->isFinished(); }), m_queries.end());
}
int AudioSourceModel::activeWorkerCount() const
{
    int count = 0;
    for (const auto &entry : m_entries) if (entry.worker->isRunning()) ++count;
    for (const auto &worker : m_retired) if (worker->isRunning()) ++count;
    return count;
}
