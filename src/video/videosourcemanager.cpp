#include "video/videosourcemanager.h"
#include <QUuid>

VideoSourceManager::VideoSourceManager(QObject *parent)
    : VideoSourceManager(QList<VideoSource>{}, parent)
{
}

VideoSourceManager::VideoSourceManager(const QList<VideoSource> &sources, QObject *parent)
    : QObject(parent), m_sources(sources)
{
    connect(this, &VideoSourceManager::currentSourceIdChanged, this,
            &VideoSourceManager::sourceMoveAvailabilityChanged);
    connect(this, &VideoSourceManager::sourcesChanged, this,
            &VideoSourceManager::sourceMoveAvailabilityChanged);
}

const VideoSource *VideoSourceManager::source(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? &m_sources.at(row) : nullptr;
}

VideoSourceStatus VideoSourceManager::sourceStatus(const QString &sourceId) const
{
    return m_statuses.value(sourceId);
}

bool VideoSourceManager::selectSource(const QString &sourceId)
{
    if (!sourceId.isEmpty() && !source(sourceId))
        return false;
    if (m_currentSourceId != sourceId) {
        m_currentSourceId = sourceId;
        emit currentSourceIdChanged();
    }
    return true;
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
            if (source.name.trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
        candidate = base + QStringLiteral(" %1").arg(number);
    }
}

bool VideoSourceManager::removeSource(const QString &sourceId)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (sourceId == m_currentSourceId)
        selectSource({});
    m_sources.removeAt(row);
    m_statuses.remove(sourceId);
    emit sourcesChanged();
    return true;
}

int VideoSourceManager::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row).id == sourceId)
            return row;
    }
    return -1;
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

bool VideoSourceManager::setSourceVisible(const QString &sourceId, bool visible)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row].visible != visible) {
        m_sources[row].visible = visible;
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::setSourceLocked(const QString &sourceId, bool locked)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row].locked != locked) {
        m_sources[row].locked = locked;
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::addSource(const VideoSource &source, const VideoSourceStatus &status)
{
    if (source.id.trimmed().isEmpty() || sourceRow(source.id) >= 0)
        return false;
    if (const auto *camera = std::get_if<CameraSourceParams>(&source.config)) {
        if (!camera->isValid() || cameraDeviceInUse(camera->deviceId))
            return false;
    }
    VideoSource initialized = source;
    if (initialized.canvasGeometry.size().isEmpty())
        initialized.canvasGeometry = VideoSourceCanvasGeometry(status.pixelSize);
    m_sources.prepend(initialized);
    m_statuses.insert(source.id, status);
    emit sourcesChanged();
    selectSource(source.id);
    return true;
}

bool VideoSourceManager::setSourcePixelSize(const QString &sourceId, const QSize &size)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &pixelSize = m_statuses[sourceId].pixelSize;
    if (pixelSize != size) {
        const QSize previous = pixelSize;
        pixelSize = size;
        if (m_sources[row].canvasGeometry.size().isEmpty() && !size.isEmpty()) {
            m_sources[row].canvasGeometry = VideoSourceCanvasGeometry(size);
        } else if (m_sources[row].type() == VideoSourceType::Camera && !size.isEmpty() &&
                   previous != size && !m_sources[row].canvasGeometry.size().isEmpty()) {
            auto &canvasGeometry = m_sources[row].canvasGeometry;
            canvasGeometry.setSize(QSizeF(canvasGeometry.size().width(),
                                     canvasGeometry.size().width() * size.height() / size.width()));
        }
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::setSourceCanvasGeometry(const QString &sourceId,
                                            const VideoSourceCanvasGeometry &canvasGeometry)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources[row].locked || !canvasGeometry.isValid() ||
        canvasGeometry.size().width() < 1 || canvasGeometry.size().height() < 1)
        return false;
    if (m_sources[row].canvasGeometry != canvasGeometry) {
        m_sources[row].canvasGeometry = canvasGeometry;
        emit sourceChanged(sourceId);
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

QList<VideoSourceFilter> VideoSourceManager::sourceFilters(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? entry->filters : QList<VideoSourceFilter>{};
}

int VideoSourceManager::filterRow(int row, const QString &filterId) const
{
    if (row < 0 || filterId.isEmpty())
        return -1;
    const auto &filters = m_sources[row].filters;
    for (int i = 0; i < filters.size(); ++i)
        if (filters[i].id == filterId)
            return i;
    return -1;
}

void VideoSourceManager::notifyFiltersChanged(int row)
{
    const QString id = m_sources[row].id;
    emit sourceChanged(id);
    emit sourceFiltersChanged(id);
}

QString VideoSourceManager::addFilter(const QString &sourceId, VideoSourceFilterType type)
{
    if (!canFilterSource(sourceId))
        return {};
    const int row = sourceRow(sourceId);
    VideoSourceFilter filter;
    switch (type) {
    case VideoSourceFilterType::ColorCorrection:
        filter.params = ColorCorrectionParams{};
        break;
    default:
        return {};
    }
    filter.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sources[row].filters.append(filter);
    notifyFiltersChanged(row);
    return filter.id;
}

bool VideoSourceManager::removeFilter(const QString &sourceId, const QString &filterId)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0)
        return false;
    m_sources[row].filters.removeAt(filter);
    notifyFiltersChanged(row);
    return true;
}

bool VideoSourceManager::moveFilter(const QString &sourceId, const QString &filterId, int offset)
{
    const int row = sourceRow(sourceId);
    const int from = filterRow(row, filterId);
    if (from < 0 || (offset != -1 && offset != 1))
        return false;
    const int to = from + offset;
    if (to < 0 || to >= m_sources[row].filters.size())
        return false;
    m_sources[row].filters.move(from, to);
    notifyFiltersChanged(row);
    return true;
}

bool VideoSourceManager::setFilterEnabled(const QString &sourceId, const QString &filterId,
                                          bool enabled)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0)
        return false;
    auto &entry = m_sources[row].filters[filter];
    if (entry.enabled != enabled) {
        entry.enabled = enabled;
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceManager::setFilterParameters(const QString &sourceId, const QString &filterId,
                                             const VideoSourceFilterParams &params)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0 || !std::visit([](const auto &value) { return value.isValid(); }, params))
        return false;
    auto &entry = m_sources[row].filters[filter];
    if (entry.params.index() != params.index())
        return false;
    if (!(entry.params == params)) {
        entry.params = params;
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceManager::canEditCurrentDisplay() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && m_sources[row].type() == VideoSourceType::DisplayCapture;
}

const DisplaySourceParams *VideoSourceManager::displaySource(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? std::get_if<DisplaySourceParams>(&entry->config) : nullptr;
}

bool VideoSourceManager::setDisplayParams(const QString &sourceId, const QString &displayId,
                                          bool cursor)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || displayId.trimmed().isEmpty())
        return false;
    auto *display = std::get_if<DisplaySourceParams>(&m_sources[row].config);
    if (!display)
        return false;
    if (display->displayId != displayId || display->captureCursor != cursor) {
        (*display) = {displayId, cursor};
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::setSourceStatus(const QString &sourceId, const QString &state,
                                         const QString &error)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &status = m_statuses[sourceId];
    if (status.state != state || status.error != error) {
        status.state = state;
        status.error = error;
        emit sourceStatusChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::canEditCurrentSource() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && (m_sources[row].type() == VideoSourceType::DisplayCapture ||
                        m_sources[row].type() == VideoSourceType::Camera);
}

bool VideoSourceManager::cameraDeviceInUse(const QString &deviceId, const QString &exceptId) const
{
    for (const auto &source : m_sources)
        if (const auto *camera = std::get_if<CameraSourceParams>(&source.config))
            if (source.id != exceptId && camera->deviceId == deviceId)
                return true;
    return false;
}

const CameraSourceParams *VideoSourceManager::cameraSource(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? std::get_if<CameraSourceParams>(&entry->config) : nullptr;
}

bool VideoSourceManager::setCameraParams(const QString &sourceId, const CameraSourceParams &value)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !value.isValid() || cameraDeviceInUse(value.deviceId, sourceId))
        return false;
    auto *camera = std::get_if<CameraSourceParams>(&m_sources[row].config);
    if (!camera)
        return false;
    if ((*camera) != value) {
        (*camera) = value;
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::setCameraActualMode(const QString &sourceId, const QSize &size,
                                             int numerator, int denominator)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources[row].type() != VideoSourceType::Camera)
        return false;
    setSourcePixelSize(sourceId, size);
    // Geometry notifications may synchronously remove the source.
    if (!source(sourceId))
        return false;
    auto &camera = m_statuses[sourceId];
    if (camera.actualFrameRateNumerator == numerator &&
        camera.actualFrameRateDenominator == denominator)
        return true;
    camera.actualFrameRateNumerator = numerator;
    camera.actualFrameRateDenominator = denominator;
    emit sourceStatusChanged(sourceId);
    return true;
}
