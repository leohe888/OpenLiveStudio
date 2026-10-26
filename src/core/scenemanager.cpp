#include "scenemanager.h"

#include <QAbstractListModel>
#include <type_traits>

SceneManager::SourceType SceneManager::SourceData::type() const
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

class SceneManager::SourceListModel : public QAbstractListModel
{
public:
    explicit SourceListModel(SceneManager *sceneManager)
        : QAbstractListModel(sceneManager), m_sceneManager(sceneManager) {}

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(sources().size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        const auto &items = sources();
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

    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "sourceId"}, {NameRole, "sourceName"}, {TypeRole, "sourceType"},
                {FilePathRole, "sourceFilePath"}, {OriginalSizeRole, "sourceOriginalSize"},
                {VisibleRole, "sourceVisible"}, {LockedRole, "sourceLocked"}};
    }

private:
    friend class SceneManager;
    const QList<SourceData> &sources() const
    {
        return m_sceneManager->m_sources;
    }

    const SceneManager *m_sceneManager;
};

SceneManager::SceneManager(QObject *parent)
    : SceneManager(QList<SourceData>{}, parent)
{
}

SceneManager::SceneManager(const QList<SourceData> &sources, QObject *parent)
    : QObject(parent), m_sources(sources), m_sourceModel(new SourceListModel(this))
{
    connect(this, &SceneManager::currentSourceIdChanged, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsMoved, this, &SceneManager::sourceMoveAvailabilityChanged);
}

QAbstractItemModel *SceneManager::sourceModel() const { return m_sourceModel; }

bool SceneManager::selectSource(const QString &id)
{
    if (!id.isEmpty()) {
        bool found = false;
        for (int row = 0; row < m_sourceModel->rowCount(); ++row) {
            if (m_sourceModel->index(row, 0).data(IdRole).toString() == id) {
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

QString SceneManager::suggestedSourceName(const QString &sceneId, const QString &baseName) const
{
    const QString base = baseName.trimmed();
    if (sceneId != currentSceneId() || base.isEmpty())
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

bool SceneManager::removeSource(const QString &sceneId, const QString &sourceId)
{
    if (sceneId != currentSceneId())
        return false;
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (sourceId == m_currentSourceId)
        selectSource({});
    m_sourceModel->beginRemoveRows({}, row, row);
    m_sources.removeAt(row);
    m_sourceModel->endRemoveRows();
    return true;
}

int SceneManager::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row).id == sourceId)
            return row;
    }
    return -1;
}

bool SceneManager::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSourceId) > 0;
}

bool SceneManager::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && row + 1 < m_sourceModel->rowCount();
}

bool SceneManager::moveSource(const QString &sourceId, int offset)
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
    m_sourceModel->beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    sources.move(from, to);
    m_sourceModel->endMoveRows();
    return true;
}

bool SceneManager::setSourceFlag(const QString &sourceId, int role, bool value)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &source = m_sources[row];
    bool &flag = role == VisibleRole ? source.visible : source.locked;
    if (flag != value) {
        flag = value;
        const auto index = m_sourceModel->index(row, 0);
        emit m_sourceModel->dataChanged(index, index, {role});
    }
    return true;
}

bool SceneManager::setSourceVisible(const QString &sourceId, bool visible)
{
    return setSourceFlag(sourceId, VisibleRole, visible);
}

bool SceneManager::setSourceLocked(const QString &sourceId, bool locked)
{
    return setSourceFlag(sourceId, LockedRole, locked);
}
