#include "sourcelistmodel.h"

#include <type_traits>

SourceListModel::SourceType SourceListModel::SourceData::type() const
{
    return std::visit([](const auto &config) {
        using Config = std::decay_t<decltype(config)>;
        if constexpr (std::is_same_v<Config, DisplayCaptureConfig>)
            return SourceType::DisplayCapture;
        else if constexpr (std::is_same_v<Config, WindowCaptureConfig>)
            return SourceType::WindowCapture;
        else if constexpr (std::is_same_v<Config, CameraConfig>)
            return SourceType::Camera;
        else if constexpr (std::is_same_v<Config, ImageConfig>)
            return SourceType::Image;
        else {
            static_assert(std::is_same_v<Config, TextConfig>);
            return SourceType::Text;
        }
    }, config);
}

SourceListModel::SourceListModel(QObject *parent)
    : SourceListModel(QList<SourceData>{}, parent)
{
}

SourceListModel::SourceListModel(const QList<SourceData> &sources, QObject *parent)
    : QAbstractListModel(parent), m_sources(sources)
{
    connect(this, &SourceListModel::currentSourceIdChanged, this, &SourceListModel::sourceMoveAvailabilityChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &SourceListModel::sourceMoveAvailabilityChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &SourceListModel::sourceMoveAvailabilityChanged);
}

int SourceListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_sources.size());
}

QVariant SourceListModel::data(const QModelIndex &index, int role) const
{
    const auto &items = m_sources;
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= items.size())
        return {};
    const auto &item = items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case NameRole:
    case Qt::DisplayRole: return item.name;
    case TypeRole:
        switch (item.type()) {
        case SourceType::DisplayCapture: return QStringLiteral("displayCapture");
        case SourceType::WindowCapture: return QStringLiteral("windowCapture");
        case SourceType::Camera: return QStringLiteral("camera");
        case SourceType::Image: return QStringLiteral("image");
        case SourceType::Text: return QStringLiteral("text");
        }
        return {};
    case FilePathRole:
        if (const auto *image = std::get_if<ImageConfig>(&item.config))
            return image->filePath;
        return {};
    case OriginalSizeRole: return item.runtime.originalSize;
    case VisibleRole: return item.visible;
    case LockedRole: return item.locked;
    default: return {};
    }
}

QHash<int, QByteArray> SourceListModel::roleNames() const
{
    return {{IdRole, "sourceId"}, {NameRole, "sourceName"}, {TypeRole, "sourceType"},
            {FilePathRole, "sourceFilePath"}, {OriginalSizeRole, "sourceOriginalSize"},
            {VisibleRole, "sourceVisible"}, {LockedRole, "sourceLocked"}};
}

bool SourceListModel::selectSource(const QString &id)
{
    if (!id.isEmpty()) {
        bool found = false;
        for (int row = 0; row < rowCount(); ++row) {
            if (index(row, 0).data(IdRole).toString() == id) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    if (m_currentSourceId != id) {
        m_currentSourceId = id;
        emit currentSourceIdChanged();
    }
    return true;
}

QString SourceListModel::suggestedSourceName(const QString &baseName) const
{
    const QString base = baseName.trimmed();
    if (base.isEmpty())
        return {};
    const auto &sources = m_sources;
    QString candidate = base;
    for (int number = 2; ; ++number) {
        bool used = false;
        for (const auto &source : sources) {
            if (source.name.trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
        candidate = base + QStringLiteral(" %1").arg(number);
    }
}

bool SourceListModel::removeSource(const QString &sourceId)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (sourceId == m_currentSourceId)
        selectSource({});
    beginRemoveRows({}, row, row);
    m_sources.removeAt(row);
    endRemoveRows();
    return true;
}

int SourceListModel::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row).id == sourceId)
            return row;
    }
    return -1;
}

bool SourceListModel::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSourceId) > 0;
}

bool SourceListModel::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && row + 1 < rowCount();
}

bool SourceListModel::moveSource(const QString &sourceId, int offset)
{
    if (offset != -1 && offset != 1)
        return false;
    const int from = sourceRow(sourceId);
    if (from < 0)
        return false;
    auto &sources = m_sources;
    const int to = from + offset;
    if (to < 0 || to >= sources.size())
        return false;
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    sources.move(from, to);
    endMoveRows();
    return true;
}

bool SourceListModel::setSourceFlag(const QString &sourceId, int role, bool value)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &source = m_sources[row];
    bool &flag = role == VisibleRole ? source.visible : source.locked;
    if (flag != value) {
        flag = value;
        const auto modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {role});
    }
    return true;
}

bool SourceListModel::setSourceVisible(const QString &sourceId, bool visible)
{
    return setSourceFlag(sourceId, VisibleRole, visible);
}

bool SourceListModel::setSourceLocked(const QString &sourceId, bool locked)
{
    return setSourceFlag(sourceId, LockedRole, locked);
}
