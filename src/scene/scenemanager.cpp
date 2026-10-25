#include "scenemanager.h"
#include "imageloader.h"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QUuid>

class SceneManager::SceneListModel : public QAbstractListModel
{
public:
    explicit SceneListModel(SceneManager *sceneManager)
        : QAbstractListModel(sceneManager), m_sceneManager(sceneManager) {}

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_sceneManager->m_scenes.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.model() != this || index.column() != 0
            || index.row() < 0 || index.row() >= m_sceneManager->m_scenes.size())
            return {};
        const auto &item = m_sceneManager->m_scenes.at(index.row());
        if (role == IdRole)
            return item.id;
        if (role == NameRole || role == Qt::DisplayRole)
            return item.name;
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {{IdRole, "sceneId"}, {NameRole, "sceneName"}};
    }

private:
    friend class SceneManager;
    const SceneManager *m_sceneManager;
};

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
        case TypeRole: return item.type;
        case FilePathRole: return item.filePath;
        case OriginalSizeRole: return item.originalSize;
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
        const int row = m_sceneManager->sceneRow(m_sceneManager->m_currentSceneId);
        if (row >= 0)
            return m_sceneManager->m_scenes.at(row).sources;
        static const QList<SourceData> empty;
        return empty;
    }

    const SceneManager *m_sceneManager;
};

SceneManager::SceneManager(QObject *parent)
    : SceneManager(QList<SceneData>{{QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  tr("场景"), {}}}, parent)
{
}

SceneManager::SceneManager(const QList<SceneData> &scenes, QObject *parent)
    : QObject(parent), m_scenes(scenes),
      m_sceneModel(new SceneListModel(this)),
      m_sourceModel(new SourceListModel(this))
{
    connect(this, &SceneManager::currentSceneIdChanged, this, &SceneManager::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsInserted, this, &SceneManager::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsRemoved, this, &SceneManager::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsMoved, this, &SceneManager::sceneMoveAvailabilityChanged);
    connect(this, &SceneManager::currentSourceIdChanged, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::modelReset, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsInserted, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, &SceneManager::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsMoved, this, &SceneManager::sourceMoveAvailabilityChanged);
    if (!m_scenes.isEmpty())
        m_currentSceneId = m_scenes.first().id;
}

QAbstractItemModel *SceneManager::sceneModel() const { return m_sceneModel; }
QAbstractItemModel *SceneManager::sourceModel() const { return m_sourceModel; }

bool SceneManager::selectScene(const QString &id)
{
    for (const auto &scene : m_scenes) {
        if (scene.id != id)
            continue;
        if (m_currentSceneId == id)
            return true;
        const bool hadSourceSelection = !m_currentSourceId.isEmpty();
        m_sourceModel->beginResetModel();
        m_currentSceneId = id;
        m_currentSourceId.clear();
        m_sourceModel->endResetModel();
        emit currentSceneIdChanged();
        if (hadSourceSelection)
            emit currentSourceIdChanged();
        return true;
    }
    return false;
}

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

QString SceneManager::suggestedSceneName() const
{
    const QString base = tr("场景");
    QString candidate = base;
    for (int number = 2; !sceneNameError(candidate).isEmpty(); ++number)
        candidate = base + QStringLiteral(" %1").arg(number);
    return candidate;
}

QString SceneManager::sceneNameError(const QString &name, const QString &excludedSceneId) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("场景名称不能为空。");
    for (const auto &scene : m_scenes) {
        if (scene.id != excludedSceneId
            && scene.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return tr("场景名称已存在。");
    }
    return {};
}

QString SceneManager::suggestedSourceName(const QString &sceneId, const QString &baseName) const
{
    const int row = sceneRow(sceneId);
    const QString base = baseName.trimmed();
    if (row < 0 || base.isEmpty())
        return {};
    const auto &sources = m_scenes.at(row).sources;
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

QString SceneManager::addScene(const QString &name)
{
    if (!sceneNameError(name).isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString trimmed = name.trimmed();
    const bool couldRemove = canRemoveCurrentScene();
    const int row = int(m_scenes.size());
    m_sceneModel->beginInsertRows({}, row, row);
    m_scenes.append({id, trimmed, {}});
    m_sceneModel->endInsertRows();
    selectScene(id);
    if (couldRemove != canRemoveCurrentScene())
        emit canRemoveCurrentSceneChanged();
    return id;
}

bool SceneManager::renameScene(const QString &id, const QString &name)
{
    for (int row = 0; row < m_scenes.size(); ++row) {
        auto &scene = m_scenes[row];
        if (scene.id != id)
            continue;
        if (!sceneNameError(name, id).isEmpty())
            return false;
        const QString trimmed = name.trimmed();
        if (scene.name != trimmed) {
            scene.name = trimmed;
            const auto index = m_sceneModel->index(row, 0);
            emit m_sceneModel->dataChanged(index, index, {NameRole, Qt::DisplayRole});
        }
        return true;
    }
    return false;
}

bool SceneManager::removeScene(const QString &id)
{
    if (m_scenes.size() <= 1)
        return false;
    for (int row = 0; row < m_scenes.size(); ++row) {
        if (m_scenes.at(row).id != id)
            continue;
        const bool couldRemove = canRemoveCurrentScene();
        if (m_currentSceneId == id) {
            const int nextRow = row + 1 < m_scenes.size() ? row + 1 : row - 1;
            selectScene(m_scenes.at(nextRow).id);
        }
        m_sceneModel->beginRemoveRows({}, row, row);
        m_scenes.removeAt(row);
        m_sceneModel->endRemoveRows();
        if (couldRemove != canRemoveCurrentScene())
            emit canRemoveCurrentSceneChanged();
        return true;
    }
    return false;
}

int SceneManager::sceneRow(const QString &id) const
{
    for (int row = 0; row < m_scenes.size(); ++row) {
        if (m_scenes.at(row).id == id)
            return row;
    }
    return -1;
}

bool SceneManager::canMoveCurrentSceneUp() const
{
    return sceneRow(m_currentSceneId) > 0;
}

bool SceneManager::canMoveCurrentSceneDown() const
{
    const int row = sceneRow(m_currentSceneId);
    return row >= 0 && row + 1 < m_scenes.size();
}

bool SceneManager::moveScene(const QString &id, int offset)
{
    if (offset != -1 && offset != 1)
        return false;
    const int from = sceneRow(id);
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= m_scenes.size())
        return false;
    m_sceneModel->beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    m_scenes.move(from, to);
    m_sceneModel->endMoveRows();
    return true;
}

QString SceneManager::validateImageSource(const QString &sceneId, const QString &name,
                                        const QString &filePath, QSize *size) const
{
    const int row = sceneRow(sceneId);
    if (row < 0)
        return tr("目标场景不存在。");
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return tr("来源名称不能为空。");
    for (const auto &source : m_scenes.at(row).sources) {
        if (source.name.trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return tr("来源名称已存在。");
    }
    const auto result = ImageLoader::load(filePath);
    if (!result.isValid())
        return result.error;
    if (size)
        *size = result.originalSize;
    return {};
}

QString SceneManager::imageSourceError(const QString &sceneId, const QString &name,
                                     const QString &filePath) const
{
    return validateImageSource(sceneId, name, filePath, nullptr);
}

QString SceneManager::addImageSource(const QString &sceneId, const QString &name,
                                   const QString &filePath)
{
    QSize size;
    if (!validateImageSource(sceneId, name, filePath, &size).isEmpty())
        return {};
    const SourceData source{QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(),
                            QStringLiteral("image"), QFileInfo(filePath).absoluteFilePath(), size};
    const bool isCurrentScene = sceneId == m_currentSceneId;
    if (isCurrentScene)
        m_sourceModel->beginInsertRows({}, 0, 0);
    m_scenes[sceneRow(sceneId)].sources.prepend(source);
    if (isCurrentScene) {
        m_sourceModel->endInsertRows();
        selectSource(source.id);
    }
    return source.id;
}

bool SceneManager::removeSource(const QString &sceneId, const QString &sourceId)
{
    const int scene = sceneRow(sceneId);
    if (scene < 0 || sourceId.isEmpty())
        return false;
    auto &sources = m_scenes[scene].sources;
    for (int row = 0; row < sources.size(); ++row) {
        if (sources.at(row).id != sourceId)
            continue;
        const bool isCurrentScene = sceneId == m_currentSceneId;
        if (isCurrentScene && sourceId == m_currentSourceId)
            selectSource({});
        if (isCurrentScene)
            m_sourceModel->beginRemoveRows({}, row, row);
        sources.removeAt(row);
        if (isCurrentScene)
            m_sourceModel->endRemoveRows();
        return true;
    }
    return false;
}

int SceneManager::sourceRow(const QString &sceneId, const QString &sourceId) const
{
    const int scene = sceneRow(sceneId);
    if (scene < 0 || sourceId.isEmpty())
        return -1;
    const auto &sources = m_scenes.at(scene).sources;
    for (int row = 0; row < sources.size(); ++row) {
        if (sources.at(row).id == sourceId)
            return row;
    }
    return -1;
}

bool SceneManager::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSceneId, m_currentSourceId) > 0;
}

bool SceneManager::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSceneId, m_currentSourceId);
    return row >= 0 && row + 1 < m_sourceModel->rowCount();
}

bool SceneManager::moveSource(const QString &sceneId, const QString &sourceId, int offset)
{
    if (offset != -1 && offset != 1)
        return false;
    const int from = sourceRow(sceneId, sourceId);
    if (from < 0)
        return false;
    auto &sources = m_scenes[sceneRow(sceneId)].sources;
    const int to = from + offset;
    if (to < 0 || to >= sources.size())
        return false;
    const bool isCurrentScene = sceneId == m_currentSceneId;
    if (isCurrentScene)
        m_sourceModel->beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    sources.move(from, to);
    if (isCurrentScene)
        m_sourceModel->endMoveRows();
    return true;
}

bool SceneManager::setSourceFlag(const QString &sceneId, const QString &sourceId, int role, bool value)
{
    const int row = sourceRow(sceneId, sourceId);
    if (row < 0)
        return false;
    auto &source = m_scenes[sceneRow(sceneId)].sources[row];
    bool &flag = role == VisibleRole ? source.visible : source.locked;
    if (flag != value) {
        flag = value;
        if (sceneId == m_currentSceneId) {
            const auto index = m_sourceModel->index(row, 0);
            emit m_sourceModel->dataChanged(index, index, {role});
        }
    }
    return true;
}

bool SceneManager::setSourceVisible(const QString &sceneId, const QString &sourceId, bool visible)
{
    return setSourceFlag(sceneId, sourceId, VisibleRole, visible);
}

bool SceneManager::setSourceLocked(const QString &sceneId, const QString &sourceId, bool locked)
{
    return setSourceFlag(sceneId, sourceId, LockedRole, locked);
}
