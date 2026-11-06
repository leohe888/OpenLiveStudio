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
            if (source->name().trimmed().compare(candidate, Qt::CaseInsensitive) == 0) {
                used = true;
                break;
            }
        }
        if (!used)
            return candidate;
        candidate = base + QStringLiteral(" %1").arg(number);
    }
}

QList<const VideoSource *> VideoSourceManager::sources() const
{
    QList<const VideoSource *> result;
    for (const auto &source : m_sources)
        result.append(source.get());
    return result;
}

QStringList VideoSourceManager::sourceIds() const
{
    QStringList result;
    for (const auto &source : m_sources)
        result.append(source->id());
    return result;
}

const VideoSource *VideoSourceManager::source(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources[row].get() : nullptr;
}

bool VideoSourceManager::addSource(std::unique_ptr<VideoSource> source)
{
    if (!source || source->id().trimmed().isEmpty() || sourceRow(source->id()) >= 0)
        return false;
    if (const auto *camera = dynamic_cast<const CameraSource *>(source.get())) {
        if (!camera->hasValidCaptureSettings() || cameraDeviceInUse(camera->deviceId()))
            return false;
    }
    if (source->transform().size().isEmpty())
        source->setTransform(VideoSourceTransform(source->pixelSize()));
    const QString id = source->id();
    m_sources.insert(m_sources.begin(), std::move(source));
    emit sourcesChanged();
    selectSource(id);
    return true;
}

bool VideoSourceManager::removeSource(const QString &sourceId)
{
    // 调用方可能直接传 source->id()，必须在销毁对象前复制。
    const QString id = sourceId;
    const int row = sourceRow(id);
    if (row < 0)
        return false;
    const bool selected = id == m_currentSourceId;
    m_sources.erase(m_sources.begin() + row);
    const bool filterSelectionCleared = m_currentFilterIds.remove(id) > 0;
    // 所有回调都观察到删除后的模型，避免重入删除或重排使旧下标失效。
    if (selected)
        selectSource({});
    emit sourcesChanged();
    if (filterSelectionCleared)
        emit currentFilterIdChanged(id);
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
    std::swap(sources[from], sources[to]);
    emit sourcesChanged();
    return true;
}

QString VideoSourceManager::sourceName(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return {};
    const auto &name = m_sources.at(row)->name();
    return name.isNull() ? QStringLiteral("") : name;
}

bool VideoSourceManager::setSourceName(const QString &sourceId, const QString &name)
{
    const int row = sourceRow(sourceId);
    const auto trimmed = name.trimmed();
    if (row < 0 || trimmed.isEmpty())
        return false;
    for (const auto &source : m_sources) {
        if (source->id() != sourceId &&
            source->name().trimmed().compare(trimmed, Qt::CaseInsensitive) == 0)
            return false;
    }
    if (m_sources[row]->name() != trimmed) {
        m_sources[row]->setName(trimmed);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

bool VideoSourceManager::sourceVisible(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && m_sources.at(row)->visible();
}

bool VideoSourceManager::setSourceVisible(const QString &sourceId, bool visible)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row]->visible() != visible) {
        m_sources[row]->setVisible(visible);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

bool VideoSourceManager::sourceLocked(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && m_sources.at(row)->locked();
}

bool VideoSourceManager::setSourceLocked(const QString &sourceId, bool locked)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (m_sources[row]->locked() != locked) {
        m_sources[row]->setLocked(locked);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

VideoSourceTransform VideoSourceManager::sourceTransform(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources.at(row)->transform() : VideoSourceTransform{};
}

bool VideoSourceManager::setSourceTransform(const QString &sourceId,
                                                 const VideoSourceTransform &transform)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || m_sources[row]->locked() || !transform.isValid() ||
        transform.size().width() < 1 || transform.size().height() < 1)
        return false;
    if (m_sources[row]->transform() != transform) {
        m_sources[row]->setTransform(transform);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

QList<VideoSourceFilter> VideoSourceManager::sourceFilters(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 ? m_sources.at(row)->filters() : QList<VideoSourceFilter>{};
}

bool VideoSourceManager::setSourceFilters(const QString &sourceId,
                                          const QList<VideoSourceFilter> &filters)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    QSet<QString> ids;
    for (const auto &filter : filters) {
        if (filter.id().trimmed().isEmpty() || ids.contains(filter.id()) ||
            !std::visit([](const auto &config) { return config.isValid(); }, filter.config()))
            return false;
        ids.insert(filter.id());
    }
    const auto &previous = m_sources.at(row)->filters();
    bool unchanged = previous.size() == filters.size();
    for (int i = 0; unchanged && i < filters.size(); ++i) {
        unchanged = previous[i].id() == filters[i].id() &&
                    previous[i].enabled() == filters[i].enabled() &&
                    previous[i].config() == filters[i].config();
    }
    if (!unchanged) {
        const QString id = sourceId;
        m_sources[row]->setFilters(filters);
        const bool selectionCleared = m_currentFilterIds.contains(id) &&
                                      !ids.contains(m_currentFilterIds.value(id));
        if (selectionCleared)
            m_currentFilterIds.remove(id);
        notifyFiltersChanged(row);
        if (selectionCleared)
            emit currentFilterIdChanged(id);
    }
    return true;
}

QString VideoSourceManager::currentFilterId(const QString &sourceId) const
{
    return m_currentFilterIds.value(sourceId);
}

bool VideoSourceManager::selectFilter(const QString &sourceId, const QString &filterId)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || (!filterId.isEmpty() && filterRow(row, filterId) < 0))
        return false;
    if (currentFilterId(sourceId) != filterId) {
        if (filterId.isEmpty())
            m_currentFilterIds.remove(sourceId);
        else
            m_currentFilterIds.insert(sourceId, filterId);
        emit currentFilterIdChanged(sourceId);
    }
    return true;
}

QString VideoSourceManager::addFilter(const QString &sourceId, VideoSourceFilterType type)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return {};
    VideoSourceFilter filter;
    switch (type) {
    case VideoSourceFilterType::ColorCorrection:
        filter.setConfig(ColorCorrectionConfig{});
        break;
    default:
        return {};
    }
    filter.setId(QUuid::createUuid().toString(QUuid::WithoutBraces));
    auto filters = m_sources[row]->filters();
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
    auto filters = m_sources[row]->filters();
    filters.removeAt(filterIndex);
    return setSourceFilters(sourceId, filters);
}

bool VideoSourceManager::canMoveFilterUp(const QString &sourceId, const QString &filterId) const
{
    return filterRow(sourceRow(sourceId), filterId) > 0;
}

bool VideoSourceManager::canMoveFilterDown(const QString &sourceId, const QString &filterId) const
{
    const int row = sourceRow(sourceId);
    const int index = filterRow(row, filterId);
    return index >= 0 && index + 1 < m_sources.at(row)->filters().size();
}

bool VideoSourceManager::moveFilter(const QString &sourceId, const QString &filterId, int offset)
{
    const int row = sourceRow(sourceId);
    const int from = filterRow(row, filterId);
    if (from < 0 || (offset != -1 && offset != 1))
        return false;
    const int to = from + offset;
    if (to < 0 || to >= m_sources[row]->filters().size())
        return false;
    auto filters = m_sources[row]->filters();
    filters.move(from, to);
    return setSourceFilters(sourceId, filters);
}

VideoSourceState VideoSourceManager::sourceState(const QString &sourceId) const
{
    const auto *item = source(sourceId);
    return item ? item->state() : VideoSourceState::Idle;
}

QString VideoSourceManager::sourceError(const QString &sourceId) const
{
    const auto *item = source(sourceId);
    return item ? item->error() : QString();
}

QSize VideoSourceManager::sourcePixelSize(const QString &sourceId) const
{
    const auto *item = source(sourceId);
    return item ? item->pixelSize() : QSize();
}

bool VideoSourceManager::updateSourceStatus(const QString &sourceId, const QSize &pixelSize,
                                            VideoSourceState state, const QString &error)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &item = *m_sources[row];
    if (item.pixelSize() == pixelSize && item.state() == state && item.error() == error)
        return true;
    const QString id = sourceId;
    const QSize previousPixelSize = item.pixelSize();
    item.updateStatus(pixelSize, state, error);
    notifySourceStatusChanged(id, previousPixelSize);
    return true;
}

bool VideoSourceManager::setSourceState(const QString &sourceId, VideoSourceState state,
                                        const QString &error)
{
    return updateSourceStatus(sourceId, sourcePixelSize(sourceId), state, error);
}

bool VideoSourceManager::setSourcePixelSize(const QString &sourceId, const QSize &pixelSize)
{
    return updateSourceStatus(sourceId, pixelSize, sourceState(sourceId), sourceError(sourceId));
}

bool VideoSourceManager::updateCameraCapture(const QString &sourceId, const QSize &pixelSize,
                                             int numerator, int denominator,
                                             const QString &description)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto *camera = dynamic_cast<CameraSource *>(m_sources[row].get());
    if (!camera)
        return false;
    if (camera->pixelSize() == pixelSize && camera->state() == VideoSourceState::Capturing &&
        camera->error().isEmpty() && camera->negotiatedFrameRateNumerator() == numerator &&
        camera->negotiatedFrameRateDenominator() == denominator &&
        camera->captureDescription() == description)
        return true;
    const QString id = sourceId;
    const QSize previousPixelSize = camera->pixelSize();
    camera->updateCapture(pixelSize, numerator, denominator, description);
    notifySourceStatusChanged(id, previousPixelSize);
    return true;
}

void VideoSourceManager::notifySourceStatusChanged(const QString &sourceId,
                                                   const QSize &previousPixelSize)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return;
    const auto size = m_sources[row]->pixelSize();
    if (previousPixelSize != size) {
        m_sources[row]->adaptTransformToPixelSize();
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    // 通知可能同步移除或重排来源，之后只能按 ID 重新查询。
    if (source(sourceId))
        emit sourceStatusChanged(sourceId);
}

bool VideoSourceManager::setCameraCaptureSettings(const QString &sourceId,
                                                  const CameraSource &requested)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || !requested.hasValidCaptureSettings() ||
        cameraDeviceInUse(requested.deviceId(), sourceId))
        return false;
    auto *camera = dynamic_cast<CameraSource *>(m_sources[row].get());
    if (!camera)
        return false;
    if (!camera->hasSameCaptureSettings(requested)) {
        camera->copyCaptureSettings(requested);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

bool VideoSourceManager::setDisplayCaptureCursor(const QString &sourceId, bool captureCursor)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto *display = dynamic_cast<DisplaySource *>(m_sources[row].get());
    if (!display)
        return false;
    if (display->captureCursor() != captureCursor) {
        display->setCaptureCursor(captureCursor);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

bool VideoSourceManager::setImageFilePath(const QString &sourceId, const QString &filePath)
{
    const int row = sourceRow(sourceId);
    if (row < 0 || filePath.trimmed().isEmpty())
        return false;
    auto *image = dynamic_cast<ImageSource *>(m_sources[row].get());
    if (!image)
        return false;
    if (image->filePath() != filePath) {
        image->setFilePath(filePath);
        const QString id = sourceId;
        emit sourceChanged(id);
    }
    return true;
}

int VideoSourceManager::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row)->id() == sourceId)
            return row;
    }
    return -1;
}

bool VideoSourceManager::cameraDeviceInUse(const QString &deviceId, const QString &exceptId) const
{
    for (const auto &source : m_sources)
        if (const auto *camera = dynamic_cast<const CameraSource *>(source.get()))
            if (source->id() != exceptId && camera->deviceId() == deviceId)
                return true;
    return false;
}

int VideoSourceManager::filterRow(int row, const QString &filterId) const
{
    if (row < 0 || filterId.isEmpty())
        return -1;
    const auto &filters = m_sources[row]->filters();
    for (int i = 0; i < filters.size(); ++i)
        if (filters[i].id() == filterId)
            return i;
    return -1;
}

void VideoSourceManager::notifyFiltersChanged(int row)
{
    const QString id = m_sources[row]->id();
    emit sourceChanged(id);
    emit sourceFiltersChanged(id);
}
