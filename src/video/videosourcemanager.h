#ifndef VIDEOSOURCEMANAGER_H
#define VIDEOSOURCEMANAGER_H

#include "video/videosource.h"

#include <QList>
#include <QHash>
#include <QObject>
#include <QString>

class VideoSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSourceManager(QObject *parent = nullptr);

    const QList<VideoSource> &sources() const
    {
        return m_sources;
    }
    QString suggestedSourceName(const QString &baseName) const;
    bool addSource(const VideoSource &source);
    // Removes the list entry only; never deletes its file.
    bool removeSource(const QString &sourceId);

    QString currentSourceId() const
    {
        return m_currentSourceId;
    }
    // An empty ID clears selection; other IDs must belong to the source list.
    bool selectSource(const QString &sourceId);
    bool canMoveCurrentSourceUp() const;
    bool canMoveCurrentSourceDown() const;
    // List order is topmost first; offset must be -1 or 1.
    // Locking only restricts future canvas geometry edits.
    bool moveSource(const QString &sourceId, int offset);

    QString sourceName(const QString &sourceId) const;
    bool setSourceName(const QString &sourceId, const QString &name);

    bool sourceVisible(const QString &sourceId) const;
    bool setSourceVisible(const QString &sourceId, bool visible);

    bool sourceLocked(const QString &sourceId) const;
    bool setSourceLocked(const QString &sourceId, bool locked);

    VideoSourceTransform sourceTransform(const QString &sourceId) const;
    bool setSourceTransform(const QString &sourceId,
                                const VideoSourceTransform &transform);

    QList<VideoSourceFilter> sourceFilters(const QString &sourceId) const;
    bool setSourceFilters(const QString &sourceId, const QList<VideoSourceFilter> &filters);
    QString currentFilterId(const QString &sourceId) const;
    // An empty filter ID clears selection; other IDs must belong to this source.
    bool selectFilter(const QString &sourceId, const QString &filterId);
    bool canFilterCurrentSource() const
    {
        return canFilterSource(m_currentSourceId);
    }
    bool canFilterSource(const QString &sourceId) const;
    QString addFilter(const QString &sourceId,
                      VideoSourceFilterType type = VideoSourceFilterType::ColorCorrection);
    bool removeFilter(const QString &sourceId, const QString &filterId);
    bool canMoveFilterUp(const QString &sourceId, const QString &filterId) const;
    bool canMoveFilterDown(const QString &sourceId, const QString &filterId) const;
    bool moveFilter(const QString &sourceId, const QString &filterId, int offset);

    VideoSourceRuntime sourceRuntime(const QString &sourceId) const;
    bool setSourceRuntime(const QString &sourceId, const VideoSourceRuntime &runtime);

    // The pointer is only valid until the next mutation.
    const VideoSourceConfig *sourceConfig(const QString &sourceId) const;
    bool setSourceConfig(const QString &sourceId, const VideoSourceConfig &config);

signals:
    // Structural changes rebuild lists; per-source changes update existing rows.
    void sourcesChanged();
    void sourceChanged(const QString &sourceId);
    void sourceRuntimeChanged(const QString &sourceId);
    void currentSourceIdChanged();
    void currentSourceMoveAvailabilityChanged();
    void sourceFiltersChanged(const QString &sourceId);
    void currentFilterIdChanged(const QString &sourceId);

private:
    int sourceRow(const QString &sourceId) const;
    bool cameraDeviceInUse(const QString &deviceId, const QString &exceptId = {}) const;
    int filterRow(int sourceRow, const QString &filterId) const;
    void notifyFiltersChanged(int row);

    QList<VideoSource> m_sources;
    QString m_currentSourceId;
    QHash<QString, QString> m_currentFilterIds;
};

#endif // VIDEOSOURCEMANAGER_H
