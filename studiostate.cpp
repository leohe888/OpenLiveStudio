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
