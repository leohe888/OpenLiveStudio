#include "video/videosourcemanager.h"
#include <QUuid>

VideoSourceManager::VideoSourceManager(QObject *parent)
    : VideoSourceManager(QList<VideoSource>{}, parent)
{
}

VideoSourceManager::VideoSourceManager(const QList<VideoSource> &sources, QObject *parent)
    : QObject(parent), m_sources(sources)
{
    for (auto &source : m_sources) {
        if (source.transform.size.isEmpty())
            source.transform = SourceGeometry::initial(source.pixelSize());
    }
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

bool VideoSourceManager::addSource(const VideoSource &source)
{
    if (source.id.trimmed().isEmpty() || sourceRow(source.id) >= 0)
        return false;
    if (const auto *camera = std::get_if<CameraSourceDetails>(&source.details)) {
        if (!camera->config.isValid() || cameraDeviceInUse(camera->config.deviceId))
            return false;
    }
    VideoSource initialized = source;
    if (initialized.transform.size.isEmpty())
        initialized.transform = SourceGeometry::initial(initialized.pixelSize());
    m_sources.prepend(initialized);
    emit sourcesChanged();
    selectSource(source.id);
    return true;
}

bool VideoSourceManager::setSourcePixelSize(const QString &sourceId, const QSize &size)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &pixelSize = std::visit(
        [](auto &source) -> QSize & {
            return source.runtime.pixelSize;
        },
        m_sources[row].details);
    if (pixelSize != size) {
        const QSize previous = pixelSize;
        pixelSize = size;
        if (m_sources[row].transform.size.isEmpty() && !size.isEmpty()) {
            m_sources[row].transform = SourceGeometry::initial(size);
        } else if (m_sources[row].type() == VideoSourceType::Camera && !size.isEmpty() &&
                   previous != size && !m_sources[row].transform.size.isEmpty()) {
            auto &transform = m_sources[row].transform;
            transform.size.setHeight(transform.size.width() * size.height() / size.width());
        }
        emit sourceChanged(sourceId);
    }
    return true;
}

bool VideoSourceManager::setSourceTransform(const QString &sourceId,
                                            const SourceTransform &transform)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources[row].locked || !SourceGeometry::isValid(transform) ||
        transform.size.width() < 1 || transform.size.height() < 1)
        return false;
    SourceTransform normalized = transform;
    normalized.rotation = SourceGeometry::normalizedAngle(normalized.rotation);
    if (m_sources[row].transform != normalized) {
        m_sources[row].transform = normalized;
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

QList<SourceFilter> VideoSourceManager::sourceFilters(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? entry->filters : QList<SourceFilter>{};
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

QString VideoSourceManager::addFilter(const QString &sourceId)
{
    if (!canFilterSource(sourceId))
        return {};
    const int row = sourceRow(sourceId);
    SourceFilter filter;
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
                                             double brightness, double contrast, double saturation)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0 || !std::isfinite(brightness) || brightness < -1 || brightness > 1 ||
        !std::isfinite(contrast) || contrast < 0 || contrast > 2 || !std::isfinite(saturation) ||
        saturation < 0 || saturation > 2)
        return false;
    auto &entry = m_sources[row].filters[filter];
    if (entry.brightness != float(brightness) || entry.contrast != float(contrast) ||
        entry.saturation != float(saturation)) {
        entry.brightness = float(brightness);
        entry.contrast = float(contrast);
        entry.saturation = float(saturation);
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceManager::canEditCurrentDisplay() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && m_sources[row].type() == VideoSourceType::DisplayCapture;
}

const DisplaySourceDetails *VideoSourceManager::displaySource(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? std::get_if<DisplaySourceDetails>(&entry->details) : nullptr;
}

bool VideoSourceManager::setDisplayConfig(const QString &sourceId, const QString &displayId,
                                          bool cursor)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || displayId.trimmed().isEmpty())
        return false;
    auto *display = std::get_if<DisplaySourceDetails>(&m_sources[row].details);
    if (!display)
        return false;
    if (display->config.displayId != displayId || display->config.captureCursor != cursor) {
        display->config = {displayId, cursor};
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
    const auto update = [&](QString &currentState, QString &currentError) {
        if (currentState == state && currentError == error)
            return false;
        currentState = state;
        currentError = error;
        return true;
    };
    auto &details = m_sources[row].details;
    bool changed = false;
    if (auto *display = std::get_if<DisplaySourceDetails>(&details))
        changed = update(display->runtime.captureState, display->runtime.captureError);
    else if (auto *camera = std::get_if<CameraSourceDetails>(&details))
        changed = update(camera->runtime.captureState, camera->runtime.captureError);
    else if (auto *image = std::get_if<ImageSourceDetails>(&details))
        changed = update(image->runtime.loadState, image->runtime.loadError);
    if (changed)
        emit sourceStatusChanged(sourceId);
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
        if (const auto *camera = std::get_if<CameraSourceDetails>(&source.details))
            if (source.id != exceptId && camera->config.deviceId == deviceId)
                return true;
    return false;
}

const CameraSourceDetails *VideoSourceManager::cameraSource(const QString &sourceId) const
{
    const auto *entry = source(sourceId);
    return entry ? std::get_if<CameraSourceDetails>(&entry->details) : nullptr;
}

bool VideoSourceManager::setCameraConfig(const QString &sourceId, const CameraConfig &value)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !value.isValid() || cameraDeviceInUse(value.deviceId, sourceId))
        return false;
    auto *camera = std::get_if<CameraSourceDetails>(&m_sources[row].details);
    if (!camera)
        return false;
    if (camera->config != value) {
        camera->config = value;
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
    auto &camera = std::get<CameraSourceDetails>(m_sources[row].details).runtime;
    if (camera.actualFrameRateNumerator == numerator &&
        camera.actualFrameRateDenominator == denominator)
        return true;
    camera.actualFrameRateNumerator = numerator;
    camera.actualFrameRateDenominator = denominator;
    emit sourceStatusChanged(sourceId);
    return true;
}
