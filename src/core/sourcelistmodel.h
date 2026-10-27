#ifndef SOURCELISTMODEL_H
#define SOURCELISTMODEL_H

#include "source.h"

#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QString>

class SourceListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString currentSourceId READ currentSourceId NOTIFY currentSourceIdChanged)
    Q_PROPERTY(bool canMoveCurrentSourceUp READ canMoveCurrentSourceUp NOTIFY sourceMoveAvailabilityChanged)
    Q_PROPERTY(bool canMoveCurrentSourceDown READ canMoveCurrentSourceDown NOTIFY sourceMoveAvailabilityChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        FilePathRole,
        OriginalSizeRole,
        VisibleRole,
        LockedRole
    };

    explicit SourceListModel(QObject *parent = nullptr);
    // Initial source IDs must be nonempty and unique.
    explicit SourceListModel(const QList<SourceData> &sources, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool addSource(const SourceData &source);
    bool setSourceOriginalSize(const QString &id, const QSize &size);

    QString currentSourceId() const { return m_currentSourceId; }
    // An empty ID clears selection; other IDs must belong to the source list.
    Q_INVOKABLE bool selectSource(const QString &id);
    Q_INVOKABLE QString suggestedSourceName(const QString &baseName) const;
    // Removes the list entry only; never deletes its file.
    Q_INVOKABLE bool removeSource(const QString &sourceId);
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
    int sourceRow(const QString &sourceId) const;
    bool setSourceFlag(const QString &sourceId, int role, bool value);
    QList<SourceData> m_sources;
    QString m_currentSourceId;
};

#endif // SOURCELISTMODEL_H
