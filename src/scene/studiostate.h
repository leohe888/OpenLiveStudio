#ifndef STUDIOSTATE_H
#define STUDIOSTATE_H

#include <QAbstractItemModel>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>

class StudioState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *sceneModel READ sceneModel CONSTANT)
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel CONSTANT)
    Q_PROPERTY(QString currentSceneId READ currentSceneId NOTIFY currentSceneIdChanged)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(bool canRemoveCurrentScene READ canRemoveCurrentScene NOTIFY canRemoveCurrentSceneChanged)
    Q_PROPERTY(bool canMoveCurrentSceneUp READ canMoveCurrentSceneUp NOTIFY sceneMoveAvailabilityChanged)
    Q_PROPERTY(bool canMoveCurrentSceneDown READ canMoveCurrentSceneDown NOTIFY sceneMoveAvailabilityChanged)
    Q_PROPERTY(bool canMoveCurrentSourceUp READ canMoveCurrentSourceUp NOTIFY sourceMoveAvailabilityChanged)
    Q_PROPERTY(bool canMoveCurrentSourceDown READ canMoveCurrentSourceDown NOTIFY sourceMoveAvailabilityChanged)

public:
    struct SourceData {
        QString id;
        QString name;
        QString type;
        QString filePath;
        QSize originalSize;
        bool visible = true;
        bool locked = false;
    };
    struct SceneData {
        QString id;
        QString name;
        QList<SourceData> sources;
    };
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        FilePathRole,
        OriginalSizeRole,
        VisibleRole,
        LockedRole
    };

    explicit StudioState(QObject *parent = nullptr);
    // Initial IDs must be nonempty and unique within each list.
    explicit StudioState(const QList<SceneData> &scenes, QObject *parent = nullptr);

    QAbstractItemModel *sceneModel() const;
    QAbstractItemModel *sourceModel() const;
    QString currentSceneId() const { return m_currentSceneId; }
    QString currentSourceId() const { return m_currentSourceId; }
    bool canRemoveCurrentScene() const { return m_scenes.size() > 1 && !m_currentSceneId.isEmpty(); }
    bool canMoveCurrentSceneUp() const;
    bool canMoveCurrentSceneDown() const;

    Q_INVOKABLE bool selectScene(const QString &id);
    // An empty ID clears selection; other IDs must belong to the current scene.
    Q_INVOKABLE bool selectSource(const QString &id);
    Q_INVOKABLE QString suggestedSceneName() const;
    // Suggest an unused name within the target scene; empty for an invalid scene or base.
    Q_INVOKABLE QString suggestedSourceName(const QString &sceneId, const QString &baseName) const;
    // Empty means valid. Exclude the target scene when checking a rename.
    Q_INVOKABLE QString sceneNameError(const QString &name, const QString &excludedSceneId = {}) const;
    // Creation returns the new ID, or an empty string if the name is invalid.
    Q_INVOKABLE QString addScene(const QString &name);
    Q_INVOKABLE bool renameScene(const QString &id, const QString &name);
    Q_INVOKABLE bool removeScene(const QString &id);
    // Move one row: offset must be -1 (up) or 1 (down).
    Q_INVOKABLE bool moveScene(const QString &id, int offset);
    // Paths are local filesystem paths. Empty error means the image can be decoded.
    Q_INVOKABLE QString imageSourceError(const QString &sceneId, const QString &name,
                                         const QString &filePath) const;
    // Returns the new ID, or empty on failure. Adds at the top of the target scene.
    Q_INVOKABLE QString addImageSource(const QString &sceneId, const QString &name,
                                       const QString &filePath);
    // Remove only this scene's entry; never delete its file or another scene's entry.
    Q_INVOKABLE bool removeSource(const QString &sceneId, const QString &sourceId);
    bool canMoveCurrentSourceUp() const;
    bool canMoveCurrentSourceDown() const;
    // List order is topmost first. Locking only restricts future transform edits.
    Q_INVOKABLE bool moveSource(const QString &sceneId, const QString &sourceId, int offset);
    Q_INVOKABLE bool setSourceVisible(const QString &sceneId, const QString &sourceId, bool visible);
    Q_INVOKABLE bool setSourceLocked(const QString &sceneId, const QString &sourceId, bool locked);

signals:
    void currentSceneIdChanged();
    void currentSourceIdChanged();
    void canRemoveCurrentSceneChanged();
    void sceneMoveAvailabilityChanged();
    void sourceMoveAvailabilityChanged();

private:
    class SceneListModel;
    class SourceListModel;
    int sceneRow(const QString &id) const;
    int sourceRow(const QString &sceneId, const QString &sourceId) const;
    bool setSourceFlag(const QString &sceneId, const QString &sourceId, int role, bool value);
    QString validateImageSource(const QString &sceneId, const QString &name,
                                const QString &filePath, QSize *size) const;
    QList<SceneData> m_scenes;
    SceneListModel *m_sceneModel;
    SourceListModel *m_sourceModel;
    QString m_currentSceneId;
    QString m_currentSourceId;
};

#endif // STUDIOSTATE_H
