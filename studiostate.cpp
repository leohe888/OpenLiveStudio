#include "studiostate.h"
#include "imageloader.h"

#include <QAbstractListModel>
#include <QFileInfo>
#include <QUuid>

class StudioState::ListModel : public QAbstractListModel
{
public:
    ListModel(const QByteArray &idRole, const QByteArray &nameRole, QObject *parent)
        : QAbstractListModel(parent), m_roles{{IdRole, idRole}, {NameRole, nameRole}}
    {
        if (idRole == "sourceId") {
            m_roles.insert(TypeRole, "sourceType");
            m_roles.insert(FilePathRole, "sourceFilePath");
            m_roles.insert(OriginalSizeRole, "sourceOriginalSize");
            m_roles.insert(VisibleRole, "sourceVisible");
            m_roles.insert(LockedRole, "sourceLocked");
        }
    }

    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_items.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.model() != this || index.column() != 0
            || index.row() < 0 || index.row() >= m_items.size())
            return {};
        const auto &item = m_items.at(index.row());
        if (role == IdRole)
            return item.id;
        if (role == NameRole || role == Qt::DisplayRole)
            return item.name;
        if (m_roles.contains(role)) {
            if (role == TypeRole)
                return item.type;
            if (role == FilePathRole)
                return item.filePath;
            if (role == OriginalSizeRole)
                return item.originalSize;
            if (role == VisibleRole)
                return item.visible;
            if (role == LockedRole)
                return item.locked;
        }
        return {};
    }

    QHash<int, QByteArray> roleNames() const override { return m_roles; }

    void replace(const QList<SourceData> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    void append(const SourceData &item)
    {
        const int row = int(m_items.size());
        beginInsertRows({}, row, row);
        m_items.append(item);
        endInsertRows();
    }

    void rename(int row, const QString &name)
    {
        m_items[row].name = name;
        emit dataChanged(index(row, 0), index(row, 0), {NameRole, Qt::DisplayRole});
    }

    void prepend(const SourceData &item)
    {
        beginInsertRows({}, 0, 0);
        m_items.prepend(item);
        endInsertRows();
    }

    void remove(int row)
    {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
    }

    void move(int from, int to)
    {
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        m_items.move(from, to);
        endMoveRows();
    }

    void setFlag(int row, int role, bool value)
    {
        if (role == VisibleRole)
            m_items[row].visible = value;
        else
            m_items[row].locked = value;
        emit dataChanged(index(row, 0), index(row, 0), {role});
    }

private:
    QHash<int, QByteArray> m_roles;
    QList<SourceData> m_items;
};

StudioState::StudioState(QObject *parent)
    : StudioState(QList<SceneData>{{QUuid::createUuid().toString(QUuid::WithoutBraces),
                                  tr("场景"), {}}}, parent)
{
}

StudioState::StudioState(const QList<SceneData> &scenes, QObject *parent)
    : QObject(parent), m_scenes(scenes),
      m_sceneModel(new ListModel("sceneId", "sceneName", this)),
      m_sourceModel(new ListModel("sourceId", "sourceName", this))
{
    connect(this, &StudioState::currentSceneIdChanged, this, &StudioState::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsInserted, this, &StudioState::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsRemoved, this, &StudioState::sceneMoveAvailabilityChanged);
    connect(m_sceneModel, &QAbstractItemModel::rowsMoved, this, &StudioState::sceneMoveAvailabilityChanged);
    connect(this, &StudioState::currentSourceIdChanged, this, &StudioState::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::modelReset, this, &StudioState::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsInserted, this, &StudioState::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsRemoved, this, &StudioState::sourceMoveAvailabilityChanged);
    connect(m_sourceModel, &QAbstractItemModel::rowsMoved, this, &StudioState::sourceMoveAvailabilityChanged);
    QList<SourceData> sceneItems;
    for (const auto &scene : m_scenes)
        sceneItems.append({scene.id, scene.name});
    m_sceneModel->replace(sceneItems);
    if (!m_scenes.isEmpty()) {
        m_currentSceneId = m_scenes.first().id;
        m_sourceModel->replace(m_scenes.first().sources);
    }
}

QAbstractItemModel *StudioState::sceneModel() const { return m_sceneModel; }
QAbstractItemModel *StudioState::sourceModel() const { return m_sourceModel; }

bool StudioState::selectScene(const QString &id)
{
    for (const auto &scene : m_scenes) {
        if (scene.id != id)
            continue;
        if (m_currentSceneId == id)
            return true;
        const bool hadSourceSelection = !m_currentSourceId.isEmpty();
        m_currentSceneId = id;
        m_currentSourceId.clear();
        m_sourceModel->replace(scene.sources);
        emit currentSceneIdChanged();
        if (hadSourceSelection)
            emit currentSourceIdChanged();
        return true;
    }
    return false;
}

bool StudioState::selectSource(const QString &id)
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

QString StudioState::suggestedSceneName() const
{
    const QString base = tr("场景");
    QString candidate = base;
    for (int number = 2; !sceneNameError(candidate).isEmpty(); ++number)
        candidate = base + QStringLiteral(" %1").arg(number);
    return candidate;
}

QString StudioState::sceneNameError(const QString &name, const QString &excludedSceneId) const
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

QString StudioState::suggestedSourceName(const QString &sceneId, const QString &baseName) const
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

QString StudioState::addScene(const QString &name)
{
    if (!sceneNameError(name).isEmpty())
        return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString trimmed = name.trimmed();
    const bool couldRemove = canRemoveCurrentScene();
    m_scenes.append({id, trimmed, {}});
    m_sceneModel->append({id, trimmed});
    selectScene(id);
    if (couldRemove != canRemoveCurrentScene())
        emit canRemoveCurrentSceneChanged();
    return id;
}

bool StudioState::renameScene(const QString &id, const QString &name)
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
            m_sceneModel->rename(row, trimmed);
        }
        return true;
    }
    return false;
}

bool StudioState::removeScene(const QString &id)
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
        m_scenes.removeAt(row);
        m_sceneModel->remove(row);
        if (couldRemove != canRemoveCurrentScene())
            emit canRemoveCurrentSceneChanged();
        return true;
    }
    return false;
}

int StudioState::sceneRow(const QString &id) const
{
    for (int row = 0; row < m_scenes.size(); ++row) {
        if (m_scenes.at(row).id == id)
            return row;
    }
    return -1;
}

bool StudioState::canMoveCurrentSceneUp() const
{
    return sceneRow(m_currentSceneId) > 0;
}

bool StudioState::canMoveCurrentSceneDown() const
{
    const int row = sceneRow(m_currentSceneId);
    return row >= 0 && row + 1 < m_scenes.size();
}

bool StudioState::moveScene(const QString &id, int offset)
{
    if (offset != -1 && offset != 1)
        return false;
    const int from = sceneRow(id);
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= m_scenes.size())
        return false;
    m_scenes.move(from, to);
    m_sceneModel->move(from, to);
    return true;
}

QString StudioState::validateImageSource(const QString &sceneId, const QString &name,
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

QString StudioState::imageSourceError(const QString &sceneId, const QString &name,
                                     const QString &filePath) const
{
    return validateImageSource(sceneId, name, filePath, nullptr);
}

QString StudioState::addImageSource(const QString &sceneId, const QString &name,
                                   const QString &filePath)
{
    QSize size;
    if (!validateImageSource(sceneId, name, filePath, &size).isEmpty())
        return {};
    const SourceData source{QUuid::createUuid().toString(QUuid::WithoutBraces), name.trimmed(),
                            QStringLiteral("image"), QFileInfo(filePath).absoluteFilePath(), size};
    m_scenes[sceneRow(sceneId)].sources.prepend(source);
    if (sceneId == m_currentSceneId) {
        m_sourceModel->prepend(source);
        selectSource(source.id);
    }
    return source.id;
}

bool StudioState::removeSource(const QString &sceneId, const QString &sourceId)
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
        sources.removeAt(row);
        if (isCurrentScene)
            m_sourceModel->remove(row);
        return true;
    }
    return false;
}

int StudioState::sourceRow(const QString &sceneId, const QString &sourceId) const
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

bool StudioState::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSceneId, m_currentSourceId) > 0;
}

bool StudioState::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSceneId, m_currentSourceId);
    return row >= 0 && row + 1 < m_sourceModel->rowCount();
}

bool StudioState::moveSource(const QString &sceneId, const QString &sourceId, int offset)
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
    sources.move(from, to);
    if (sceneId == m_currentSceneId)
        m_sourceModel->move(from, to);
    return true;
}

bool StudioState::setSourceFlag(const QString &sceneId, const QString &sourceId, int role, bool value)
{
    const int row = sourceRow(sceneId, sourceId);
    if (row < 0)
        return false;
    auto &source = m_scenes[sceneRow(sceneId)].sources[row];
    bool &flag = role == VisibleRole ? source.visible : source.locked;
    if (flag != value) {
        flag = value;
        if (sceneId == m_currentSceneId)
            m_sourceModel->setFlag(row, role, value);
    }
    return true;
}

bool StudioState::setSourceVisible(const QString &sceneId, const QString &sourceId, bool visible)
{
    return setSourceFlag(sceneId, sourceId, VisibleRole, visible);
}

bool StudioState::setSourceLocked(const QString &sceneId, const QString &sourceId, bool locked)
{
    return setSourceFlag(sceneId, sourceId, LockedRole, locked);
}
