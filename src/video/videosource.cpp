#include "video/videosource.h"

VideoSource::~VideoSource() = default;

VideoSource::VideoSource(const QString &id, const QString &name) : m_id(id), m_name(name)
{
}

const QString &VideoSource::id() const
{
    return m_id;
}

const QString &VideoSource::name() const
{
    return m_name;
}

void VideoSource::setName(const QString &name)
{
    m_name = name;
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

const VideoSourceTransform &VideoSource::transform() const
{
    return m_transform;
}

void VideoSource::setTransform(const VideoSourceTransform &transform)
{
    m_transform = transform;
}

const QList<VideoSourceFilter> &VideoSource::filters() const
{
    return m_filters;
}

void VideoSource::setFilters(const QList<VideoSourceFilter> &filters)
{
    m_filters = filters;
}

QSize VideoSource::pixelSize() const
{
    return m_pixelSize;
}

VideoSourceState VideoSource::state() const
{
    return m_state;
}

const QString &VideoSource::error() const
{
    return m_error;
}

void VideoSource::updateStatus(const QSize &pixelSize, VideoSourceState state, const QString &error)
{
    m_pixelSize = pixelSize;
    m_state = state;
    m_error = error;
}

void VideoSource::adaptTransformToPixelSize()
{
    if (m_transform.size().isEmpty() && !m_pixelSize.isEmpty())
        m_transform = VideoSourceTransform(m_pixelSize);
}

DisplaySource::DisplaySource(const QString &id, const QString &name, bool captureCursor)
    : VideoSource(id, name), m_captureCursor(captureCursor)
{
}

bool DisplaySource::captureCursor() const
{
    return m_captureCursor;
}

void DisplaySource::setCaptureCursor(bool captureCursor)
{
    m_captureCursor = captureCursor;
}

CameraSource::CameraSource(const QString &id, const QString &name, const QString &deviceId,
                           const QSize &resolution, int numerator, int denominator,
                           const QString &pixelFormat)
    : VideoSource(id, name), m_deviceId(deviceId), m_pixelFormat(pixelFormat),
      m_resolution(resolution), m_frameRateNumerator(numerator), m_frameRateDenominator(denominator)
{
}

const QString &CameraSource::deviceId() const
{
    return m_deviceId;
}

void CameraSource::setDeviceId(const QString &deviceId)
{
    m_deviceId = deviceId;
}

const QString &CameraSource::pixelFormat() const
{
    return m_pixelFormat;
}

void CameraSource::setPixelFormat(const QString &format)
{
    m_pixelFormat = format;
}

const QSize &CameraSource::resolution() const
{
    return m_resolution;
}

void CameraSource::setResolution(const QSize &resolution)
{
    m_resolution = resolution;
}

int CameraSource::frameRateNumerator() const
{
    return m_frameRateNumerator;
}

void CameraSource::setFrameRateNumerator(int numerator)
{
    m_frameRateNumerator = numerator;
}

int CameraSource::frameRateDenominator() const
{
    return m_frameRateDenominator;
}

void CameraSource::setFrameRateDenominator(int denominator)
{
    m_frameRateDenominator = denominator;
}

bool CameraSource::hasValidCaptureSettings() const
{
    return !m_deviceId.trimmed().isEmpty() && m_frameRateDenominator > 0 &&
           ((m_resolution.isEmpty() && m_frameRateNumerator == 0) ||
            (m_resolution.isValid() && !m_resolution.isEmpty() && m_frameRateNumerator > 0));
}

bool CameraSource::hasSameCaptureSettings(const CameraSource &other) const
{
    return m_pixelFormat == other.m_pixelFormat && m_deviceId == other.m_deviceId &&
           m_resolution == other.m_resolution &&
           m_frameRateNumerator == other.m_frameRateNumerator &&
           m_frameRateDenominator == other.m_frameRateDenominator;
}

void CameraSource::copyCaptureSettings(const CameraSource &other)
{
    m_deviceId = other.m_deviceId;
    m_pixelFormat = other.m_pixelFormat;
    m_resolution = other.m_resolution;
    m_frameRateNumerator = other.m_frameRateNumerator;
    m_frameRateDenominator = other.m_frameRateDenominator;
}

int CameraSource::negotiatedFrameRateNumerator() const
{
    return m_negotiatedFrameRateNumerator;
}

int CameraSource::negotiatedFrameRateDenominator() const
{
    return m_negotiatedFrameRateDenominator;
}

const QString &CameraSource::captureDescription() const
{
    return m_captureDescription;
}

void CameraSource::updateCapture(const QSize &pixelSize, int numerator, int denominator,
                                 const QString &description)
{
    m_negotiatedFrameRateNumerator = numerator;
    m_negotiatedFrameRateDenominator = denominator;
    m_captureDescription = description;
    updateStatus(pixelSize, VideoSourceState::Capturing);
}

void CameraSource::adaptTransformToPixelSize()
{
    if (transform().size().isEmpty()) {
        VideoSource::adaptTransformToPixelSize();
        return;
    }
    const auto size = pixelSize();
    if (size.isEmpty())
        return;
    auto geometry = transform();
    geometry.setSize(
        QSizeF(geometry.size().width(), geometry.size().width() * size.height() / size.width()));
    setTransform(geometry);
}

ImageSource::ImageSource(const QString &id, const QString &name, const QString &filePath)
    : VideoSource(id, name), m_filePath(filePath)
{
}

const QString &ImageSource::filePath() const
{
    return m_filePath;
}

void ImageSource::setFilePath(const QString &filePath)
{
    m_filePath = filePath;
}
