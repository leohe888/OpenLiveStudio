#pragma once

#include "video/videosourcemanager.h"

// Preserve fixture order and start without a selection.
inline bool addSources(VideoSourceManager &manager, const QList<VideoSource> &sources)
{
    for (auto it = sources.crbegin(); it != sources.crend(); ++it) {
        if (!manager.addSource(*it))
            return false;
    }
    return manager.selectSource({});
}

inline bool setSourceStatus(VideoSourceManager &manager, const QString &id,
                            VideoSourceState state, const QString &error)
{
    auto runtime = manager.sourceRuntime(id);
    runtime.setState(state);
    runtime.setError(error);
    return manager.setSourceRuntime(id, runtime);
}

inline bool setSourcePixelSize(VideoSourceManager &manager, const QString &id, const QSize &size)
{
    auto runtime = manager.sourceRuntime(id);
    runtime.setPixelSize(size);
    return manager.setSourceRuntime(id, runtime);
}

inline const CameraSourceConfig *cameraSource(const VideoSourceManager &manager, const QString &id)
{
    const auto *config = manager.sourceConfig(id);
    return config ? std::get_if<CameraSourceConfig>(config) : nullptr;
}

inline const DisplaySourceConfig *displaySource(const VideoSourceManager &manager, const QString &id)
{
    const auto *config = manager.sourceConfig(id);
    return config ? std::get_if<DisplaySourceConfig>(config) : nullptr;
}

inline bool setCameraActualMode(VideoSourceManager &manager, const QString &id, const QSize &size,
                                int numerator, int denominator)
{
    if (!cameraSource(manager, id))
        return false;
    auto runtime = manager.sourceRuntime(id);
    runtime.setPixelSize(size);
    runtime.setActualFrameRateNumerator(numerator);
    runtime.setActualFrameRateDenominator(denominator);
    return manager.setSourceRuntime(id, runtime);
}

inline bool setDisplayConfig(VideoSourceManager &manager, const QString &id,
                              const QString &displayId, bool cursor)
{
    return manager.setSourceConfig(id, DisplaySourceConfig{displayId, cursor});
}

inline bool setCameraConfig(VideoSourceManager &manager, const QString &id,
                             const CameraSourceConfig &config)
{
    return manager.setSourceConfig(id, config);
}

inline bool setFilterEnabled(VideoSourceManager &manager, const QString &id,
                              const QString &filterId, bool enabled)
{
    auto filters = manager.sourceFilters(id);
    for (auto &filter : filters) {
        if (filter.id() == filterId) {
            filter.setEnabled(enabled);
            return manager.setSourceFilters(id, filters);
        }
    }
    return false;
}

inline bool setFilterConfig(VideoSourceManager &manager, const QString &id,
                             const QString &filterId, const VideoSourceFilterConfig &config)
{
    auto filters = manager.sourceFilters(id);
    for (auto &filter : filters) {
        if (filter.id() == filterId) {
            filter.setConfig(config);
            return manager.setSourceFilters(id, filters);
        }
    }
    return false;
}
