#pragma once

#include "video/videosourcemanager.h"
#include "video/camera/camerasourcecontroller.h"

inline CameraSource cameraRequest(const QString &deviceId, const QSize &resolution = {},
                                  int numerator = 0, int denominator = 1)
{
    return CameraSource{{}, {}, deviceId, resolution, numerator, denominator};
}

inline CameraSource cameraForMode(const CameraMode &mode, const QString &deviceId)
{
    auto source = cameraRequest(deviceId);
    mode.applyTo(source);
    return source;
}

inline QString addCamera(CameraSourceController &controller, const QString &name,
                         const CameraSource &source)
{
    CameraMode mode;
    mode.width = source.resolution().width();
    mode.height = source.resolution().height();
    mode.frameRateNumerator = source.frameRateNumerator();
    mode.frameRateDenominator = source.frameRateDenominator();
    mode.subtype = source.pixelFormat();
    return controller.addCamera(name, source.deviceId(), mode);
}

inline std::unique_ptr<VideoSource> copySource(const VideoSource &source)
{
    if (const auto *camera = dynamic_cast<const CameraSource *>(&source))
        return std::make_unique<CameraSource>(*camera);
    if (const auto *image = dynamic_cast<const ImageSource *>(&source))
        return std::make_unique<ImageSource>(*image);
    return std::make_unique<DisplaySource>(dynamic_cast<const DisplaySource &>(source));
}

// 夹具拥有模型值；管理器接收其独立副本的所有权。
class SourceFixture
{
public:
    SourceFixture(const QString &id, const QString &name, bool cursor = true)
        : m_source(std::make_unique<DisplaySource>(id, name, cursor))
    {
    }

    SourceFixture(const QString &id, const QString &name, const char *path)
        : SourceFixture(id, name, QString::fromUtf8(path))
    {
    }

    SourceFixture(const QString &id, const QString &name, const QString &path)
        : m_source(std::make_unique<ImageSource>(id, name, path))
    {
    }

    SourceFixture(const QString &id, const QString &name, const CameraSource &requested)
        : m_source(std::make_unique<CameraSource>(
              id, name, requested.deviceId(), requested.resolution(),
              requested.frameRateNumerator(), requested.frameRateDenominator(),
              requested.pixelFormat()))
    {
    }

    SourceFixture(const VideoSource &source) : m_source(copySource(source))
    {
    }

    SourceFixture(const SourceFixture &other) : m_source(copySource(*other.m_source))
    {
    }

    SourceFixture(SourceFixture &&) = default;
    SourceFixture &operator=(SourceFixture &&) = default;

    SourceFixture &operator=(const SourceFixture &other)
    {
        m_source = copySource(*other.m_source);
        return *this;
    }

    template <class Input>
    SourceFixture(const QString &id, const QString &name, bool visible, bool locked,
                  const VideoSourceTransform &transform, const QList<VideoSourceFilter> &filters,
                  const QSize &pixelSize, const Input &input)
        : SourceFixture(id, name, input)
    {
        m_source->setVisible(visible);
        m_source->setLocked(locked);
        m_source->setTransform(transform);
        m_source->setFilters(filters);
        m_source->updateStatus(pixelSize, VideoSourceState::Idle);
    }

    QString id() const
    {
        return m_source->id();
    }

    std::unique_ptr<VideoSource> copy() const
    {
        return copySource(*m_source);
    }

private:
    std::unique_ptr<VideoSource> m_source;
};

inline bool addSource(VideoSourceManager &manager, const SourceFixture &source)
{
    return manager.addSource(source.copy());
}

inline bool addSources(VideoSourceManager &manager, const QList<SourceFixture> &sources)
{
    for (auto it = sources.crbegin(); it != sources.crend(); ++it)
        if (!manager.addSource(it->copy()))
            return false;
    return manager.selectSource({});
}

inline bool addSources(VideoSourceManager &manager, const QList<const VideoSource *> &sources)
{
    for (auto it = sources.crbegin(); it != sources.crend(); ++it)
        if (!manager.addSource(copySource(**it)))
            return false;
    return manager.selectSource({});
}

inline bool setSourceStatus(VideoSourceManager &manager, const QString &id, VideoSourceState state,
                            const QString &error)
{
    return manager.setSourceState(id, state, error);
}

inline bool setSourcePixelSize(VideoSourceManager &manager, const QString &id, const QSize &size)
{
    return manager.setSourcePixelSize(id, size);
}

inline const CameraSource *cameraSource(const VideoSourceManager &manager, const QString &id)
{
    return dynamic_cast<const CameraSource *>(manager.source(id));
}

inline const DisplaySource *displaySource(const VideoSourceManager &manager, const QString &id)
{
    return dynamic_cast<const DisplaySource *>(manager.source(id));
}

inline const ImageSource *imageSource(const VideoSourceManager &manager, const QString &id)
{
    return dynamic_cast<const ImageSource *>(manager.source(id));
}

inline bool setCameraActualMode(VideoSourceManager &manager, const QString &id, const QSize &size,
                                int n, int d)
{
    return manager.updateCameraCapture(id, size, n, d);
}

inline bool setDisplayCursor(VideoSourceManager &manager, const QString &id, bool cursor)
{
    return manager.setDisplayCaptureCursor(id, cursor);
}

inline bool setCameraSettings(VideoSourceManager &manager, const QString &id,
                              const CameraSource &source)
{
    return manager.setCameraCaptureSettings(id, source);
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
