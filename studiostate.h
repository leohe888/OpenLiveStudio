#ifndef STUDIOSTATE_H
#define STUDIOSTATE_H

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QString>

class StudioState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *sceneModel READ sceneModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel CONSTANT)
    Q_PROPERTY(QString currentSceneId READ currentSceneId NOTIFY currentSceneIdChanged)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)

public:
    struct SourceData { QString id; QString name; };
    struct SceneData { QString id; QString name; QList<SourceData> sources; };
    enum Role { IdRole = Qt::UserRole + 1, NameRole };

    explicit StudioState(QObject *parent = nullptr);
    // Initial IDs must be nonempty and unique within each list.
    explicit StudioState(const QList<SceneData> &scenes, QObject *parent = nullptr);

    QAbstractItemModel *sceneModel() const;
    QAbstractItemModel *sourceModel() const;
    QString currentSceneId() const { return m_currentSceneId; }
    QString currentSourceId() const { return m_currentSourceId; }

    Q_INVOKABLE bool selectScene(const QString &id);
    // An empty ID clears selection; other IDs must belong to the current scene.
    Q_INVOKABLE bool selectSource(const QString &id);
    Q_INVOKABLE QString suggestedSceneName() const;
    // Empty means valid. Exclude the target scene when checking a rename.
    Q_INVOKABLE QString sceneNameError(const QString &name, const QString &excludedSceneId = {}) const;
    // Creation returns the new ID, or an empty string if the name is invalid.
    Q_INVOKABLE QString addScene(const QString &name);
    Q_INVOKABLE bool renameScene(const QString &id, const QString &name);

signals:
    void currentSceneIdChanged();
    void currentSourceIdChanged();

private:
    class ListModel;
    QList<SceneData> m_scenes;
    ListModel *m_sceneModel;
    ListModel *m_sourceModel;
    QString m_currentSceneId;
    QString m_currentSourceId;
};

#endif // STUDIOSTATE_H
