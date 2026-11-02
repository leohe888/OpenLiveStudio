#include "video/videosourcemanager.h"
#include <QUuid>
#include <QSet>

VideoSourceManager::VideoSourceManager(QObject *parent)
    : QObject(parent)
{
    connect(this, &VideoSourceManager::currentSourceIdChanged, this,
            &VideoSourceManager::currentSourceMoveAvailabilityChanged);
    connect(this, &VideoSourceManager::sourcesChanged, this,
            &VideoSourceManager::currentSourceMoveAvailabilityChanged);
}

QString VideoSourceManager::suggestedSourceName(const QString &baseName) const
{
    const QString base = baseName.trimmed();
    if (base.isEmpty())
        return {};
    const auto &sources = m_sources;
    QString candidate = base;
    for (int number = 2;; ++number) {
        bool used = false;
        for (const auto &source : sources) {
            if (source.name().trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
        candidate = base + QStringLiteral(" %1").arg(number);
    }
}

bool VideoSourceManager::addSource(const VideoSource &source)
{
    if (source.id().trimmed().isEmpty() || sourceRow(source.id()) >= 0)
        return false;
    if (const auto *camera = std::get_if<CameraSourceConfig>(&source.config())) {
        if (!camera->isValid() || cameraDeviceInUse(camera->deviceId()))
            return false;
    }
    VideoSource initialized = source;
    if (initialized.canvasGeometry().size().isEmpty())
        initialized.setCanvasGeometry(VideoSourceCanvasGeometry(source.runtime().pixelSize()));
    m_sources.prepend(initialized);
    emit sourcesChanged();
    selectSource(source.id());
    return true;
}

bool VideoSourceManager::removeSource(const QString &sourceId)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (sourceId == m_currentSourceId)
        selectSource({});
    m_sources.removeAt(row);
    emit sourcesChanged();
    return true;
}

bool VideoSourceManager::selectSource(const QString &sourceId)
{
    if (!sourceId.isEmpty() && sourceRow(sourceId) < 0)
        return false;
    if (m_currentSourceId != sourceId) {
        m_currentSourceId = sourceId;
        emit currentSourceIdChanged();
    }
    return true;
}

bool VideoSourceManager::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSourceId) > 0;
}

bool VideoSourceManager::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && row + 1 < m_sources.size();
}

bool VideoSourceManager::moveSource(const QString &sourceId, int offset)
{
    if (offset != -1 && offset != 1)
        return false;
    const int from = sourceRow(sourceId);
    if (from < 0)
        return false;
    auto &sources = m_sources;
    const int to = from + offset;
    if (to < 0 || to >= sources.size())
        return false;
    sources.move(from, to);
    emit sourcesChanged();
    return true;
}

QString VideoSourceManager::sourceName(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return {};
    const auto &name = m_sources.at(row).name();
    return name.isNull() ? QStringLiteral("") : name;
}

bool VideoSourceManager::setSourceName(const QString &sourceId, const QString &name)
{
    const int row = sourceRow(sourceId);
    const auto trimmed = name.trimmed();
    if (row < 0 || trimmed.isEmpty())
        return false;
    for (const auto &source : m_sources) {
        if (source.id() != sourceId &&
            source.name().trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return false;
    }
    if (m_sources[row].name() != trimmed) {
        m_sources[row].setName(trimmed);
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::sourceVisible(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && m_sources.at(row).visible();
}

bool VideoSourceManager::setSourceVisible(const QString &sourceId, bool visible)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row].visible() != visible) {
        m_sources[row].setVisible(visible);
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::sourceLocked(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && m_sources.at(row).locked();
}

bool VideoSourceManager::setSourceLocked(const QString &sourceId, bool locked)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row].locked() != locked) {
        m_sources[row].setLocked(locked);
        emit sourceChanged(sourceId);
    }
    return true;
}

VideoSourceCanvasGeometry VideoSourceManager::sourceCanvasGeometry(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources.at(row).canvasGeometry() : VideoSourceCanvasGeometry{};
}

bool VideoSourceManager::setSourceCanvasGeometry(const QString &sourceId,
                                                 const VideoSourceCanvasGeometry &canvasGeometry)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources[row].locked() || !canvasGeometry.isValid() ||
        canvasGeometry.size().width() < 1 || canvasGeometry.size().height() < 1)
        return false;
    if (m_sources[row].canvasGeometry() != canvasGeometry) {
        m_sources[row].setCanvasGeometry(canvasGeometry);
        emit sourceChanged(sourceId);
    }
    return true;
}

QList<VideoSourceFilter> VideoSourceManager::sourceFilters(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources.at(row).filters() : QList<VideoSourceFilter>{};
}

bool VideoSourceManager::setSourceFilters(const QString &sourceId,
                                          const QList<VideoSourceFilter> &filters)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !canFilterSource(sourceId))
        return false;
    QSet<QString> ids;
    for (const auto &filter : filters) {
        if (filter.id().trimmed().isEmpty() || ids.contains(filter.id()) ||
            !std::visit([](const auto &config) { return config.isValid(); }, filter.config()))
            return false;
        ids.insert(filter.id());
    }
    const auto &previous = m_sources.at(row).filters();
    bool unchanged = previous.size() == filters.size();
    for (int i = 0; unchanged && i < filters.size(); ++i) {
        unchanged = previous[i].id() == filters[i].id() &&
                    previous[i].enabled() == filters[i].enabled() &&
                    previous[i].config() == filters[i].config();
    }
    if (!unchanged) {
        m_sources[row].setFilters(filters);
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceManager::canFilterSource(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && (m_sources[row].type() == VideoSourceType::Image ||
                        m_sources[row].type() == VideoSourceType::DisplayCapture ||
                        m_sources[row].type() == VideoSourceType::Camera);
}

QString VideoSourceManager::addFilter(const QString &sourceId, VideoSourceFilterType type)
{
    if (!canFilterSource(sourceId))
        return {};
    const int row = sourceRow(sourceId);
    VideoSourceFilter filter;
    switch (type) {
    case VideoSourceFilterType::ColorCorrection:
        filter.setConfig(ColorCorrectionConfig{});
        break;
    default:
        return {};
    }
    filter.setId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto filters = m_sources[row].filters();
    filters.append(filter);
    if (!setSourceFilters(sourceId, filters))
        return {};
    return filter.id();
}

bool VideoSourceManager::removeFilter(const QString &sourceId, const QString &filterId)
{
    const int row = sourceRow(sourceId);
    const int filterIndex = filterRow(row, filterId);
    if (filterIndex < 0)
        return false;
    auto filters = m_sources[row].filters();
    filters.removeAt(filterIndex);
    return setSourceFilters(sourceId, filters);
}

bool VideoSourceManager::moveFilter(const QString &sourceId, const QString &filterId, int offset)
{
    const int row = sourceRow(sourceId);
    const int from = filterRow(row, filterId);
    if (from < 0 || (offset != -1 && offset != 1))
        return false;
    const int to = from + offset;
    if (to < 0 || to >= m_sources[row].filters().size())
        return false;
    auto filters = m_sources[row].filters();
    filters.move(from, to);
    return setSourceFilters(sourceId, filters);
}

VideoSourceRuntime VideoSourceManager::sourceRuntime(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources.at(row).runtime() : VideoSourceRuntime{};
}

bool VideoSourceManager::setSourceRuntime(const QString &sourceId, const VideoSourceRuntime &runtime)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    const auto previous = m_sources.at(row).runtime();
    const bool sizeChanged = previous.pixelSize() != runtime.pixelSize();
    const bool changed = sizeChanged || previous.state() != runtime.state() ||
                         previous.error() != runtime.error() ||
                         previous.actualFrameRateNumerator() != runtime.actualFrameRateNumerator() ||
                         previous.actualFrameRateDenominator() != runtime.actualFrameRateDenominator();
    if (!changed)
        return true;
    m_sources[row].setRuntime(runtime);
    if (sizeChanged) {
        const auto size = runtime.pixelSize();
        auto geometry = m_sources.at(row).canvasGeometry();
        if (geometry.size().isEmpty() && !size.isEmpty()) {
            m_sources[row].setCanvasGeometry(VideoSourceCanvasGeometry(size));
        } else if (m_sources.at(row).type() == VideoSourceType::Camera && !size.isEmpty() &&
                   !geometry.size().isEmpty()) {
            geometry.setSize(QSizeF(geometry.size().width(),
                                    geometry.size().width() * size.height() / size.width()));
            m_sources[row].setCanvasGeometry(geometry);
        }
        emit sourceChanged(sourceId);
    }
    // Geometry notifications may synchronously remove or reorder the source.
    if (sourceRow(sourceId) >= 0)
        emit sourceRuntimeChanged(sourceId);
    return true;
}

const VideoSourceConfig *VideoSourceManager::sourceConfig(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? &m_sources.at(row).config() : nullptr;
}

bool VideoSourceManager::setSourceConfig(const QString &sourceId, const VideoSourceConfig &config)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources.at(row).config().index() != config.index())
        return false;
    const auto &previous = m_sources.at(row).config();
    bool unchanged = false;
    if (const auto *camera = std::get_if<CameraSourceConfig>(&config)) {
        if (!camera->isValid() || cameraDeviceInUse(camera->deviceId(), sourceId))
            return false;
        unchanged = std::get<CameraSourceConfig>(previous) == *camera;
    } else if (const auto *display = std::get_if<DisplaySourceConfig>(&config)) {
        if (display->displayId().trimmed().isEmpty())
            return false;
        const auto &old = std::get<DisplaySourceConfig>(previous);
        unchanged = old.displayId() == display->displayId() &&
                    old.captureCursor() == display->captureCursor();
    } else {
        const auto &image = std::get<ImageSourceConfig>(config);
        if (image.filePath().trimmed().isEmpty())
            return false;
        unchanged = std::get<ImageSourceConfig>(previous).filePath() == image.filePath();
    }
    if (!unchanged) {
        m_sources[row].setConfig(config);
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::canEditCurrentSourceProperties() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && (m_sources[row].type() == VideoSourceType::DisplayCapture ||
                        m_sources[row].type() == VideoSourceType::Camera);
}

int VideoSourceManager::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row).id() == sourceId)
            return row;
    }
    return -1;
}

bool VideoSourceManager::cameraDeviceInUse(const QString &deviceId, const QString &exceptId) const
{
    for (const auto &source : m_sources)
        if (const auto *camera = std::get_if<CameraSourceConfig>(&source.config()))
            if (source.id() != exceptId && camera->deviceId() == deviceId)
                return true;
    return false;
}

int VideoSourceManager::filterRow(int row, const QString &filterId) const
{
    if (row < 0 || filterId.isEmpty())
        return -1;
    const auto &filters = m_sources[row].filters();
    for (int i = 0; i < filters.size(); ++i)
        if (filters[i].id() == filterId)
            return i;
    return -1;
}

void VideoSourceManager::notifyFiltersChanged(int row)
{
    const QString id = m_sources[row].id();
    emit sourceChanged(id);
    emit sourceFiltersChanged(id);
}
