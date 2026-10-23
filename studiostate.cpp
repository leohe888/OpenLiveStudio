#include "studiostate.h"

#include <QAbstractListModel>
#include <QUuid>

class StudioState::ListModel : public QAbstractListModel
{
public:
    ListModel(const QByteArray &idRole, const QByteArray &nameRole, QObject *parent)
        : QAbstractListModel(parent), m_roles{{IdRole, idRole}, {NameRole, nameRole}} {}

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

    void remove(int row)
    {
        beginRemoveRows({}, row, row);
        m_items.removeAt(row);
        endRemoveRows();
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
