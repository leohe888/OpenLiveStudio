#ifndef VIDEOSOURCEMANAGER_H
#define VIDEOSOURCEMANAGER_H

#include "video/videosource.h"

#include <QList>
#include <QObject>
#include <QString>

class VideoSourceManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoSourceManager(QObject *parent = nullptr);
    // Initial source IDs must be nonempty and unique.
    explicit VideoSourceManager(const QList<VideoSource> &sources, QObject *parent = nullptr);

    const QList<VideoSource> &sources() const
    {
        return m_sources;
    }

    // The pointer is only valid until the next mutation; do not retain it across signals.
    const VideoSource *source(const QString &sourceId) const;

    bool setSourceTransform(const QString &sourceId, const VideoSourceTransform &transform);
    bool addSource(const VideoSource &source);
    bool setSourcePixelSize(const QString &sourceId, const QSize &size);
    bool setSourceStatus(const QString &sourceId, const QString &state, const QString &error);
    bool canEditCurrentDisplay() const;
    bool canEditCurrentSource() const;
    const CameraSourceDetails *cameraSource(const QString &sourceId) const;
    bool setCameraConfig(const QString &sourceId, const CameraConfig &config);
    bool cameraDeviceInUse(const QString &deviceId, const QString &exceptId = {}) const;
    bool setCameraActualMode(const QString &sourceId, const QSize &size, int numerator,
                             int denominator);

    const DisplaySourceDetails *displaySource(const QString &sourceId) const;
    bool setDisplayConfig(const QString &sourceId, const QString &displayId, bool cursor);

    QString currentSourceId() const
    {
        return m_currentSourceId;
    }

    // An empty ID clears selection; other IDs must belong to the source list.
    bool selectSource(const QString &sourceId);
    QString suggestedSourceName(const QString &baseName) const;
    // Removes the list entry only; never deletes its file.
    bool removeSource(const QString &sourceId);
    bool canMoveCurrentSourceUp() const;
    bool canMoveCurrentSourceDown() const;
    // List order is topmost first; offset must be -1 or 1.
    // Locking only restricts future transform edits.
    bool moveSource(const QString &sourceId, int offset);
    bool setSourceVisible(const QString &sourceId, bool visible);
    bool setSourceLocked(const QString &sourceId, bool locked);

    bool canFilterCurrentSource() const
    {
        return canFilterSource(m_currentSourceId);
    }

    bool canFilterSource(const QString &sourceId) const;
    QList<VideoSourceFilter> sourceFilters(const QString &sourceId) const;
    QString addFilter(const QString &sourceId);
    bool removeFilter(const QString &sourceId, const QString &filterId);
    bool moveFilter(const QString &sourceId, const QString &filterId, int offset);
    bool setFilterEnabled(const QString &sourceId, const QString &filterId, bool enabled);
    bool setFilterParameters(const QString &sourceId, const QString &filterId, double brightness,
                             double contrast, double saturation);

signals:
    // Structural changes rebuild lists; per-source changes update existing rows.
    void sourcesChanged();
    void sourceChanged(const QString &sourceId);
    void sourceStatusChanged(const QString &sourceId);
    void currentSourceIdChanged();
    void sourceMoveAvailabilityChanged();
    void sourceFiltersChanged(const QString &sourceId);

private:
    int sourceRow(const QString &sourceId) const;
    int filterRow(int sourceRow, const QString &filterId) const;
    void notifyFiltersChanged(int row);
    QList<VideoSource> m_sources;
    QString m_currentSourceId;
};

#endif // VIDEOSOURCEMANAGER_H
