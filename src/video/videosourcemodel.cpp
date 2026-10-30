#include "video/videosourcemodel.h"
#include <QUuid>

VideoSourceModel::VideoSourceModel(QObject *parent)
    : VideoSourceModel(QList<SourceData>{}, parent)
{
}

VideoSourceModel::VideoSourceModel(const QList<SourceData> &sources, QObject *parent)
    : QAbstractListModel(parent), m_sources(sources)
{
    for (auto &source : m_sources) {
        if (source.transform.size.isEmpty())
            source.transform = SourceGeometry::initial(source.runtime.originalSize);
    }
    connect(this, &VideoSourceModel::currentSourceIdChanged, this, &VideoSourceModel::sourceMoveAvailabilityChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &VideoSourceModel::sourceMoveAvailabilityChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &VideoSourceModel::sourceMoveAvailabilityChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &VideoSourceModel::sourceMoveAvailabilityChanged);
}

int VideoSourceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_sources.size());
}

QVariant VideoSourceModel::data(const QModelIndex &index, int role) const
{
    const auto &items = m_sources;
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= items.size())
        return {};
    const auto &item = items.at(index.row());
    switch (role) {
    case IdRole: return item.id;
    case NameRole:
    case Qt::DisplayRole: return item.name;
    case TypeRole:
        switch (item.type()) {
        case SourceType::DisplayCapture: return QStringLiteral("displayCapture");
        case SourceType::WindowCapture: return QStringLiteral("windowCapture");
        case SourceType::Camera: return QStringLiteral("camera");
        case SourceType::Image: return QStringLiteral("image");
        case SourceType::Text: return QStringLiteral("text");
        }
        return {};
    case FilePathRole:
        if (const auto *image = std::get_if<ImageConfig>(&item.config))
            return image->filePath;
        return {};
    case OriginalSizeRole: return item.runtime.originalSize;
    case VisibleRole: return item.visible;
    case LockedRole: return item.locked;
    case TransformRole: return QVariant::fromValue(item.transform);
    case FiltersRole: return QVariant::fromValue(item.filters);
    case DisplayConfigRole:
        if (const auto *display = std::get_if<DisplayCaptureConfig>(&item.config))
            return QVariantMap{{"displayId", display->displayId}, {"captureCursor", display->captureCursor}};
        return {};
    case CameraConfigRole:
        if (const auto *camera = std::get_if<CameraConfig>(&item.config))
            return QVariantMap{{"deviceId", camera->deviceId}, {"width", camera->requestedResolution.width()},
                {"height", camera->requestedResolution.height()}, {"numerator", camera->requestedFrameRate},
                {"denominator", camera->frameRateDenominator}};
        return {};
    case CaptureStateRole: return item.runtime.captureState;
    case CaptureErrorRole: return item.runtime.captureError;
    default: return {};
    }
}

QHash<int, QByteArray> VideoSourceModel::roleNames() const
{
    return {{IdRole, "sourceId"}, {NameRole, "sourceName"}, {TypeRole, "sourceType"},
            {FilePathRole, "sourceFilePath"}, {OriginalSizeRole, "sourceOriginalSize"},
            {VisibleRole, "sourceVisible"}, {LockedRole, "sourceLocked"}, {TransformRole, "sourceTransform"},
            {FiltersRole, "sourceFilters"}, {DisplayConfigRole, "sourceDisplayConfig"},
            {CameraConfigRole, "sourceCameraConfig"}, {CaptureStateRole, "sourceCaptureState"}, {CaptureErrorRole, "sourceCaptureError"}};
}

bool VideoSourceModel::selectSource(const QString &id)
{
    if (!id.isEmpty()) {
        bool found = false;
        for (int row = 0; row < rowCount(); ++row) {
            if (index(row, 0).data(IdRole).toString() == id) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    if (m_currentSourceId != id) {
        m_currentSourceId = id;
        emit currentSourceIdChanged();
    }
    return true;
}

QString VideoSourceModel::suggestedSourceName(const QString &baseName) const
{
    const QString base = baseName.trimmed();
    if (base.isEmpty())
        return {};
    const auto &sources = m_sources;
    QString candidate = base;
    for (int number = 2; ; ++number) {
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

bool VideoSourceModel::removeSource(const QString &sourceId)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    if (sourceId == m_currentSourceId)
        selectSource({});
    beginRemoveRows({}, row, row);
    m_sources.removeAt(row);
    endRemoveRows();
    return true;
}

int VideoSourceModel::sourceRow(const QString &sourceId) const
{
    if (sourceId.isEmpty())
        return -1;
    for (int row = 0; row < m_sources.size(); ++row) {
        if (m_sources.at(row).id == sourceId)
            return row;
    }
    return -1;
}

bool VideoSourceModel::canMoveCurrentSourceUp() const
{
    return sourceRow(m_currentSourceId) > 0;
}

bool VideoSourceModel::canMoveCurrentSourceDown() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && row + 1 < rowCount();
}

bool VideoSourceModel::moveSource(const QString &sourceId, int offset)
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
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    sources.move(from, to);
    endMoveRows();
    return true;
}

bool VideoSourceModel::setSourceFlag(const QString &sourceId, int role, bool value)
{
    const int row = sourceRow(sourceId);
    if (row < 0)
        return false;
    auto &source = m_sources[row];
    bool &flag = role == VisibleRole ? source.visible : source.locked;
    if (flag != value) {
        flag = value;
        const auto modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex, {role});
    }
    return true;
}

bool VideoSourceModel::setSourceVisible(const QString &sourceId, bool visible)
{
    return setSourceFlag(sourceId, VisibleRole, visible);
}

bool VideoSourceModel::setSourceLocked(const QString &sourceId, bool locked)
{
    return setSourceFlag(sourceId, LockedRole, locked);
}

bool VideoSourceModel::addSource(const SourceData &source)
{
    if (source.id.trimmed().isEmpty() || sourceRow(source.id) >= 0)
        return false;
    if (const auto *camera = std::get_if<CameraConfig>(&source.config)) {
        if (!camera->valid() || cameraDeviceInUse(camera->deviceId)) return false;
    }
    beginInsertRows({}, 0, 0);
    SourceData initialized = source;
    if (initialized.transform.size.isEmpty())
        initialized.transform = SourceGeometry::initial(initialized.runtime.originalSize);
    m_sources.prepend(initialized);
    endInsertRows();
    selectSource(source.id);
    return true;
}

bool VideoSourceModel::setSourceOriginalSize(const QString &id, const QSize &size)
{
    const int row = sourceRow(id);
    if (row < 0) return false;
    if (m_sources[row].runtime.originalSize != size) {
        const QSize previous = m_sources[row].runtime.originalSize;
        m_sources[row].runtime.originalSize = size;
        QList<int> roles{OriginalSizeRole};
        if (m_sources[row].transform.size.isEmpty() && !size.isEmpty()) {
            m_sources[row].transform = SourceGeometry::initial(size);
            roles.append(TransformRole);
        } else if (m_sources[row].type() == SourceType::Camera && !size.isEmpty()
                   && previous != size && !m_sources[row].transform.size.isEmpty()) {
            auto &transform = m_sources[row].transform;
            transform.size.setHeight(transform.size.width() * size.height() / size.width());
            roles.append(TransformRole);
        }
        emit dataChanged(index(row, 0), index(row, 0), roles);
    }
    return true;
}

bool VideoSourceModel::setSourceTransform(const QString &id, const SourceTransform &transform)
{
    const int row = sourceRow(id);
    if (row < 0 || m_sources[row].locked || !SourceGeometry::valid(transform)
        || transform.size.width() < 1 || transform.size.height() < 1)
        return false;
    SourceTransform normalized = transform;
    normalized.rotation = SourceGeometry::normalizedAngle(normalized.rotation);
    if (m_sources[row].transform != normalized) {
        m_sources[row].transform = normalized;
        emit dataChanged(index(row, 0), index(row, 0), {TransformRole});
    }
    return true;
}

bool VideoSourceModel::canFilterSource(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    return row >= 0 && (m_sources[row].type() == SourceType::Image || m_sources[row].type() == SourceType::DisplayCapture || m_sources[row].type() == SourceType::Camera);
}

QVariantList VideoSourceModel::sourceFilters(const QString &sourceId) const
{
    const int row = sourceRow(sourceId);
    if (row < 0) return {};
    QVariantList result;
    for (const auto &filter : m_sources[row].filters)
        result.append(QVariantMap{{"id", filter.id}, {"type", "colorCorrection"},
            {"enabled", filter.enabled}, {"brightness", filter.brightness},
            {"contrast", filter.contrast}, {"saturation", filter.saturation}});
    return result;
}

int VideoSourceModel::filterRow(int row, const QString &filterId) const
{
    if (row < 0 || filterId.isEmpty()) return -1;
    const auto &filters = m_sources[row].filters;
    for (int i = 0; i < filters.size(); ++i)
        if (filters[i].id == filterId) return i;
    return -1;
}

void VideoSourceModel::notifyFiltersChanged(int row)
{
    const QString id = m_sources[row].id;
    emit dataChanged(index(row, 0), index(row, 0), {FiltersRole});
    emit sourceFiltersChanged(id);
}

QString VideoSourceModel::addColorCorrection(const QString &sourceId)
{
    if (!canFilterSource(sourceId)) return {};
    const int row = sourceRow(sourceId);
    SourceFilter filter;
    filter.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_sources[row].filters.append(filter);
    notifyFiltersChanged(row);
    return filter.id;
}

bool VideoSourceModel::removeFilter(const QString &sourceId, const QString &filterId)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0) return false;
    m_sources[row].filters.removeAt(filter);
    notifyFiltersChanged(row);
    return true;
}

bool VideoSourceModel::moveFilter(const QString &sourceId, const QString &filterId, int offset)
{
    const int row = sourceRow(sourceId);
    const int from = filterRow(row, filterId);
    if (from < 0 || (offset != -1 && offset != 1)) return false;
    const int to = from + offset;
    if (to < 0 || to >= m_sources[row].filters.size()) return false;
    m_sources[row].filters.move(from, to);
    notifyFiltersChanged(row);
    return true;
}

bool VideoSourceModel::setFilterEnabled(const QString &sourceId, const QString &filterId, bool enabled)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0) return false;
    auto &entry = m_sources[row].filters[filter];
    if (entry.enabled != enabled) {
        entry.enabled = enabled;
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceModel::setFilterParameters(const QString &sourceId, const QString &filterId,
                                         double brightness, double contrast, double saturation)
{
    const int row = sourceRow(sourceId);
    const int filter = filterRow(row, filterId);
    if (filter < 0 || !std::isfinite(brightness) || brightness < -1 || brightness > 1
        || !std::isfinite(contrast) || contrast < 0 || contrast > 2
        || !std::isfinite(saturation) || saturation < 0 || saturation > 2) return false;
    auto &entry = m_sources[row].filters[filter];
    if (entry.brightness != float(brightness) || entry.contrast != float(contrast)
        || entry.saturation != float(saturation)) {
        entry.brightness = float(brightness);
        entry.contrast = float(contrast);
        entry.saturation = float(saturation);
        notifyFiltersChanged(row);
    }
    return true;
}

bool VideoSourceModel::canEditCurrentDisplay() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && m_sources[row].type() == SourceType::DisplayCapture;
}

QVariantMap VideoSourceModel::displayProperties(const QString &id) const
{
    const int row = sourceRow(id);
    if (row < 0) return {};
    const auto *config = std::get_if<DisplayCaptureConfig>(&m_sources[row].config);
    if (!config) return {};
    return {{"displayId", config->displayId}, {"captureCursor", config->captureCursor},
            {"state", m_sources[row].runtime.captureState}, {"error", m_sources[row].runtime.captureError}};
}

bool VideoSourceModel::setDisplayConfig(const QString &id, const QString &displayId, bool cursor)
{
    const int row = sourceRow(id);
    if (row < 0 || displayId.trimmed().isEmpty()) return false;
    auto *config = std::get_if<DisplayCaptureConfig>(&m_sources[row].config);
    if (!config) return false;
    if (config->displayId != displayId || config->captureCursor != cursor) {
        *config = {displayId, cursor};
        emit dataChanged(index(row, 0), index(row, 0), {DisplayConfigRole});
    }
    return true;
}

bool VideoSourceModel::setSourceCaptureStatus(const QString &id, const QString &state, const QString &error)
{
    const int row = sourceRow(id);
    if (row < 0) return false;
    auto &runtime = m_sources[row].runtime;
    if (runtime.captureState != state || runtime.captureError != error) {
        runtime.captureState = state;
        runtime.captureError = error;
        emit dataChanged(index(row, 0), index(row, 0), {CaptureStateRole, CaptureErrorRole});
        emit captureStatusChanged(id);
    }
    return true;
}

bool VideoSourceModel::canEditCurrentSource() const
{
    const int row = sourceRow(m_currentSourceId);
    return row >= 0 && (m_sources[row].type() == SourceType::DisplayCapture || m_sources[row].type() == SourceType::Camera);
}

bool VideoSourceModel::cameraDeviceInUse(const QString &deviceId, const QString &exceptId) const
{
    for (const auto &source : m_sources)
        if (const auto *config = std::get_if<CameraConfig>(&source.config))
            if (source.id != exceptId && config->deviceId == deviceId) return true;
    return false;
}

QVariantMap VideoSourceModel::cameraProperties(const QString &id) const
{
    const int row = sourceRow(id);
    if (row < 0 || m_sources[row].type() != SourceType::Camera) return {};
    auto result = data(index(row, 0), CameraConfigRole).toMap();
    const auto &runtime = m_sources[row].runtime;
    result.insert("state", runtime.captureState); result.insert("error", runtime.captureError);
    result.insert("actualWidth", runtime.originalSize.width()); result.insert("actualHeight", runtime.originalSize.height());
    result.insert("actualNumerator", runtime.actualRateNumerator); result.insert("actualDenominator", runtime.actualRateDenominator);
    return result;
}

bool VideoSourceModel::setCameraConfig(const QString &id, const CameraConfig &value)
{
    const int row = sourceRow(id);
    if (row < 0 || !value.valid() || cameraDeviceInUse(value.deviceId, id)) return false;
    auto *config = std::get_if<CameraConfig>(&m_sources[row].config);
    if (!config) return false;
    if (*config != value) {
        *config = value;
        emit dataChanged(index(row, 0), index(row, 0), {CameraConfigRole});
    }
    return true;
}

void VideoSourceModel::setCameraActualMode(const QString &id, const QSize &size, int numerator, int denominator)
{
    const int row = sourceRow(id);
    if (row < 0) return;
    setSourceOriginalSize(id, size);
    auto &runtime = m_sources[row].runtime;
    if (runtime.actualRateNumerator == numerator && runtime.actualRateDenominator == denominator) return;
    runtime.actualRateNumerator = numerator; runtime.actualRateDenominator = denominator;
    emit captureStatusChanged(id);
}
