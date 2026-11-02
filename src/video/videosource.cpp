#include "video/videosource.h"

#include <type_traits>

DisplaySourceConfig::DisplaySourceConfig() = default;

DisplaySourceConfig::DisplaySourceConfig(const QString &displayId, bool captureCursor)
    : m_displayId(displayId), m_captureCursor(captureCursor)
{
}

const QString &DisplaySourceConfig::displayId() const
{
    return m_displayId;
}

void DisplaySourceConfig::setDisplayId(const QString &displayId)
{
    m_displayId = displayId;
}

bool DisplaySourceConfig::captureCursor() const
{
    return m_captureCursor;
}

void DisplaySourceConfig::setCaptureCursor(bool captureCursor)
{
    m_captureCursor = captureCursor;
}

CameraSourceConfig::CameraSourceConfig() = default;

CameraSourceConfig::CameraSourceConfig(const QString &deviceId, const QSize &resolution,
                                       int frameRateNumerator, int frameRateDenominator)
    : m_deviceId(deviceId), m_resolution(resolution), m_frameRateNumerator(frameRateNumerator),
      m_frameRateDenominator(frameRateDenominator)
{
}

const QString &CameraSourceConfig::deviceId() const
{
    return m_deviceId;
}

void CameraSourceConfig::setDeviceId(const QString &deviceId)
{
    m_deviceId = deviceId;
}

const QSize &CameraSourceConfig::resolution() const
{
    return m_resolution;
}

void CameraSourceConfig::setResolution(const QSize &resolution)
{
    m_resolution = resolution;
}

int CameraSourceConfig::frameRateNumerator() const
{
    return m_frameRateNumerator;
}

void CameraSourceConfig::setFrameRateNumerator(int frameRateNumerator)
{
    m_frameRateNumerator = frameRateNumerator;
}

int CameraSourceConfig::frameRateDenominator() const
{
    return m_frameRateDenominator;
}

void CameraSourceConfig::setFrameRateDenominator(int frameRateDenominator)
{
    m_frameRateDenominator = frameRateDenominator;
}

ImageSourceConfig::ImageSourceConfig() = default;

ImageSourceConfig::ImageSourceConfig(const QString &filePath) : m_filePath(filePath)
{
}

const QString &ImageSourceConfig::filePath() const
{
    return m_filePath;
}

void ImageSourceConfig::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
}

VideoSourceRuntime::VideoSourceRuntime() = default;

VideoSourceRuntime::VideoSourceRuntime(const QSize &pixelSize, const QString &state,
                                       const QString &error, int actualFrameRateNumerator,
                                       int actualFrameRateDenominator)
    : m_pixelSize(pixelSize), m_state(state), m_error(error),
      m_actualFrameRateNumerator(actualFrameRateNumerator),
      m_actualFrameRateDenominator(actualFrameRateDenominator)
{
}

const QSize &VideoSourceRuntime::pixelSize() const
{
    return m_pixelSize;
}

void VideoSourceRuntime::setPixelSize(const QSize &pixelSize)
{
    m_pixelSize = pixelSize;
}

const QString &VideoSourceRuntime::state() const
{
    return m_state;
}

void VideoSourceRuntime::setState(const QString &state)
{
    m_state = state;
}

const QString &VideoSourceRuntime::error() const
{
    return m_error;
}

void VideoSourceRuntime::setError(const QString &error)
{
    m_error = error;
}

int VideoSourceRuntime::actualFrameRateNumerator() const
{
    return m_actualFrameRateNumerator;
}

void VideoSourceRuntime::setActualFrameRateNumerator(int actualFrameRateNumerator)
{
    m_actualFrameRateNumerator = actualFrameRateNumerator;
}

int VideoSourceRuntime::actualFrameRateDenominator() const
{
    return m_actualFrameRateDenominator;
}

void VideoSourceRuntime::setActualFrameRateDenominator(int actualFrameRateDenominator)
{
    m_actualFrameRateDenominator = actualFrameRateDenominator;
}

VideoSource::VideoSource() = default;

VideoSource::VideoSource(const QString &id, const QString &name, const VideoSourceConfig &config,
                         bool visible, bool locked, const VideoSourceCanvasGeometry &canvasGeometry,
                         const QList<VideoSourceFilter> &filters, const VideoSourceRuntime &runtime)
    : m_id(id), m_name(name), m_config(config), m_visible(visible), m_locked(locked),
      m_canvasGeometry(canvasGeometry), m_filters(filters), m_runtime(runtime)
{
}

const QString &VideoSource::id() const
{
    return m_id;
}

void VideoSource::setId(const QString &id)
{
    m_id = id;
}

const QString &VideoSource::name() const
{
    return m_name;
}

void VideoSource::setName(const QString &name)
{
    m_name = name;
}

const VideoSourceConfig &VideoSource::config() const
{
    return m_config;
}

void VideoSource::setConfig(const VideoSourceConfig &config)
{
    m_config = config;
}

bool VideoSource::visible() const
{
    return m_visible;
}

void VideoSource::setVisible(bool visible)
{
    m_visible = visible;
}

bool VideoSource::locked() const
{
    return m_locked;
}

void VideoSource::setLocked(bool locked)
{
    m_locked = locked;
}

const VideoSourceCanvasGeometry &VideoSource::canvasGeometry() const
{
    return m_canvasGeometry;
}

void VideoSource::setCanvasGeometry(const VideoSourceCanvasGeometry &canvasGeometry)
{
    m_canvasGeometry = canvasGeometry;
}

const QList<VideoSourceFilter> &VideoSource::filters() const
{
    return m_filters;
}

void VideoSource::setFilters(const QList<VideoSourceFilter> &filters)
{
    m_filters = filters;
}

const VideoSourceRuntime &VideoSource::runtime() const
{
    return m_runtime;
}

void VideoSource::setRuntime(const VideoSourceRuntime &runtime)
{
    m_runtime = runtime;
}

bool CameraSourceConfig::isValid() const
{
    return !m_deviceId.trimmed().isEmpty() && m_frameRateDenominator > 0 &&
           ((m_resolution.isEmpty() && m_frameRateNumerator == 0) ||
            (m_resolution.isValid() && !m_resolution.isEmpty() && m_frameRateNumerator > 0));
}

bool CameraSourceConfig::operator==(const CameraSourceConfig &other) const
{
    return m_deviceId == other.m_deviceId && m_resolution == other.m_resolution &&
           m_frameRateNumerator == other.m_frameRateNumerator &&
           m_frameRateDenominator == other.m_frameRateDenominator;
}

bool CameraSourceConfig::operator!=(const CameraSourceConfig &other) const
{
    return !(*this == other);
}

VideoSourceType VideoSource::type() const
{
    return std::visit(
        [](const auto &source) {
            using Source = std::decay_t<decltype(source)>;
            if constexpr (std::is_same_v<Source, DisplaySourceConfig>)
                return VideoSourceType::DisplayCapture;
            else if constexpr (std::is_same_v<Source, CameraSourceConfig>)
                return VideoSourceType::Camera;
            else {
                static_assert(std::is_same_v<Source, ImageSourceConfig>);
                return VideoSourceType::Image;
            }
        },
        m_config);
}
