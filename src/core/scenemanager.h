#ifndef SCENEMANAGER_H
#define SCENEMANAGER_H

#include <QAbstractItemModel>
#include <QColor>
#include <QFont>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <variant>

class SceneManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel CONSTANT)
    // Temporary compatibility token for source QML; there is no scene selection.
    Q_PROPERTY(QString currentSceneId READ currentSceneId CONSTANT)
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(bool canMoveCurrentSourceUp READ canMoveCurrentSourceUp NOTIFY sourceMoveAvailabilityChanged)
    Q_PROPERTY(bool canMoveCurrentSourceDown READ canMoveCurrentSourceDown NOTIFY sourceMoveAvailabilityChanged)

public:
    enum class SourceType {
        DisplayCapture,
        WindowCapture,
        Camera,
        Image,
        Text
    };
    Q_ENUM(SourceType)

    struct DisplayCaptureConfig {
        QString displayId;
        bool captureCursor = true;
    };
    struct WindowCaptureConfig {
        QString executablePath;
        QString windowClass;
        QString windowTitle;
        bool captureCursor = true;
    };
    struct CameraConfig {
        QString deviceId;
        // Invalid size and zero frame rate mean no explicit preference.
        QSize requestedResolution;
        int requestedFrameRate = 0;
    };
    struct ImageConfig {
        QString filePath;
    };
    struct TextConfig {
        QString text;
        QFont font;
        QColor color = Qt::white;
    };
    using SourceConfig = std::variant<
        DisplayCaptureConfig,
        WindowCaptureConfig,
        CameraConfig,
        ImageConfig,
        TextConfig
    >;

    struct SourceRuntimeInfo {
        // Invalid until loading, capture, or rendering supplies the actual size.
        QSize originalSize;
    };
    struct SourceData {
        QString id;
        QString name;
        SourceConfig config = DisplayCaptureConfig{};
        SourceRuntimeInfo runtime;
        bool visible = true;
        bool locked = false;

        SourceType type() const;
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

    explicit SceneManager(QObject *parent = nullptr);
    // Initial source IDs must be nonempty and unique.
    explicit SceneManager(const QList<SourceData> &sources, QObject *parent = nullptr);

    QAbstractItemModel *sourceModel() const;
    // Transitional source API adapter; not a stored or selectable scene ID.
    QString currentSceneId() const { return QStringLiteral("canvas"); }
    QString currentSourceId() const { return m_currentSourceId; }
    // An empty ID clears selection; other IDs must belong to the source list.
    Q_INVOKABLE bool selectSource(const QString &id);
    Q_INVOKABLE QString suggestedSourceName(const QString &baseName) const;
    // Removes the list entry only; never deletes its file.
    Q_INVOKABLE bool removeSource(const QString &sourceId);
    // Temporary dialog adapters: sceneId must match currentSceneId().
    Q_INVOKABLE QString suggestedSourceName(const QString &sceneId, const QString &baseName) const;
    Q_INVOKABLE bool removeSource(const QString &sceneId, const QString &sourceId);
    bool canMoveCurrentSourceUp() const;
    bool canMoveCurrentSourceDown() const;
    // List order is topmost first; offset must be -1 or 1.
    // Locking only restricts future transform edits.
    Q_INVOKABLE bool moveSource(const QString &sourceId, int offset);
    Q_INVOKABLE bool setSourceVisible(const QString &sourceId, bool visible);
    Q_INVOKABLE bool setSourceLocked(const QString &sourceId, bool locked);

signals:
    void currentSourceIdChanged();
    void sourceMoveAvailabilityChanged();

private:
    class SourceListModel;
    int sourceRow(const QString &sourceId) const;
    bool setSourceFlag(const QString &sourceId, int role, bool value);
    QList<SourceData> m_sources;
    SourceListModel *m_sourceModel;
    QString m_currentSourceId;
};

#endif // SCENEMANAGER_H
