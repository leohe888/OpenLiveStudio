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
    // 仅移除来源，不删除其引用的本地文件。
    bool removeSource(const QString &sourceId);

    QString currentSourceId() const
    {
        return m_currentSourceId;
    }
    // 空 ID 表示清除选择；非空 ID 必须属于当前来源列表。
    bool selectSource(const QString &sourceId);
    bool canMoveCurrentSourceUp() const;
    bool canMoveCurrentSourceDown() const;
    // 列表首项位于画面最上层，offset 仅允许 -1 或 1。
    // 锁定只限制画布变换编辑，仍允许调整层级。
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
    // 空滤镜 ID 表示清除选择；非空 ID 必须属于指定来源。
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

    // 指针仅在下一次修改前有效，修改来源后必须重新查询。
    const VideoSourceConfig *sourceConfig(const QString &sourceId) const;
    bool setSourceConfig(const QString &sourceId, const VideoSourceConfig &config);

signals:
    // 结构变化通知用于重建列表；单个来源的变化通知用于更新已有行。
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

#endif
