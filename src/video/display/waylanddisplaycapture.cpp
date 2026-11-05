#include "waylanddisplaycapture.h"
#include "util/mediaclock.h"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QScopeGuard>
#include <QTimer>
#include <QUuid>
#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/param.h>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace {
constexpr auto service = "org.freedesktop.portal.Desktop";
constexpr auto path = "/org/freedesktop/portal/desktop";
constexpr auto screencast = "org.freedesktop.portal.ScreenCast";
QString token() { return "ols_" + QUuid::createUuid().toString(QUuid::Id128); }
QDBusMessage call(const QString &object, const QString &interface, const QString &method)
{
    return QDBusMessage::createMethodCall(service, object, interface, method);
}
struct StreamCapture {
    pw_stream *stream = nullptr;
    spa_video_info_raw format{};
    LatestCaptureFrameSlot *frames = nullptr;
    quint64 generation = 0, sequence = 0;
    qint64 lastFrame = 0;
    QString error;

    static void stateChanged(void *data, pw_stream_state, pw_stream_state state, const char *error)
    {
        auto &self = *static_cast<StreamCapture *>(data);
        if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
            self.error = QObject::tr("显示器视频流已断开：%1").arg(QString::fromUtf8(error ? error : ""));
    }
    static void paramChanged(void *data, uint32_t id, const spa_pod *param)
    {
        auto &self = *static_cast<StreamCapture *>(data);
        if (id != SPA_PARAM_Format || !param)
            return;
        if (spa_format_video_raw_parse(param, &self.format) < 0) {
            self.error = QObject::tr("无法解析显示器视频格式。");
            return;
        }
        // Only mapped CPU buffers; never negotiate DMA-BUF for this backend.
        uint8_t storage[256];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        const spa_pod *params[] = {static_cast<spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
            SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(
                (1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))))};
        pw_stream_update_params(self.stream, params, 1);
    }
    static void process(void *data)
    {
        auto &self = *static_cast<StreamCapture *>(data);
        pw_buffer *buffer = nullptr;
        // Drain the queue and copy only the newest frame.
        while (auto *next = pw_stream_dequeue_buffer(self.stream)) {
            if (buffer)
                pw_stream_queue_buffer(self.stream, buffer);
            buffer = next;
        }
        if (!buffer)
            return;
        const auto requeue = qScopeGuard([&] { pw_stream_queue_buffer(self.stream, buffer); });
        const auto now = MediaClock::nowNs();
        if (now - self.lastFrame < 33333334 || !buffer->buffer->n_datas)
            return;
        QString error;
        auto pixels = WaylandDisplayCapture::copyPixels(*buffer->buffer, self.format, error);
        if (!error.isEmpty())
            self.error = error;
        if (!pixels)
            return;
        auto frame = std::make_shared<CaptureFrame>();
        frame->size = pixels->planeSizes[0];
        frame->pixels = std::move(pixels);
        frame->timestampNs = now;
        frame->sequence = ++self.sequence;
        frame->generation = self.generation;
        self.frames->publish(std::move(frame));
        self.lastFrame = now;
    }
};
}

std::shared_ptr<const PixelFrame> WaylandDisplayCapture::copyPixels(
    const spa_buffer &buffer, const spa_video_info_raw &format, QString &error)
{
    error.clear();
    if (!buffer.n_datas)
        return {};
    const auto &plane = buffer.datas[0];
    const auto width = format.size.width, height = format.size.height;
    const auto fmt = format.format;
    if (!plane.data || !plane.chunk || !plane.chunk->size ||
        (plane.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
        return {};
    if (!width || !height || width > 32768 || height > 32768 ||
        (fmt != SPA_VIDEO_FORMAT_BGRA && fmt != SPA_VIDEO_FORMAT_BGRx &&
         fmt != SPA_VIDEO_FORMAT_RGBA && fmt != SPA_VIDEO_FORMAT_RGBx)) {
        error = QObject::tr("显示器视频格式或尺寸不受支持。");
        return {};
    }
    const qint64 rowBytes = qint64(width) * 4;
    const qint64 stride = plane.chunk->stride;
    const quint64 offset = plane.chunk->offset;
    const quint64 bytes = quint64(height - 1) * quint64(qMax<qint64>(stride, 0)) + rowBytes;
    if (stride < rowBytes || offset > plane.maxsize || bytes > plane.maxsize - offset ||
        bytes > plane.chunk->size || rowBytes * height > std::numeric_limits<int>::max()) {
        error = QObject::tr("显示器视频缓冲区布局无效。");
        return {};
    }
    auto pixels = std::make_shared<PixelFrame>();
    pixels->layout = (fmt == SPA_VIDEO_FORMAT_BGRA || fmt == SPA_VIDEO_FORMAT_BGRx)
                         ? PixelFrame::BGRA : PixelFrame::RGBA;
    pixels->fullRange = true;
    pixels->planeSizes[0] = QSize(width, height);
    pixels->planes[0].resize(rowBytes * height);
    auto *dst = reinterpret_cast<unsigned char *>(pixels->planes[0].data());
    const auto *src = static_cast<const unsigned char *>(plane.data) + offset;
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(dst + y * rowBytes, src + y * stride, rowBytes);
        // Desktop streams are opaque, including the undefined X byte of BGRx/RGBx.
        for (uint32_t x = 0; x < width; ++x)
            dst[y * rowBytes + x * 4 + 3] = 255;
    }
    return pixels;
}

WaylandDisplayCapture::WaylandDisplayCapture()
    : m_connectionName(token()),
      m_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_connectionName))
{
}
WaylandDisplayCapture::~WaylandDisplayCapture()
{
    if (!m_session.isEmpty())
        m_bus.asyncCall(call(m_session, "org.freedesktop.portal.Session", "Close"));
    QDBusConnection::disconnectFromBus(m_connectionName);
}
uint WaylandDisplayCapture::cursorModes()
{
    auto message = call(path, "org.freedesktop.DBus.Properties", "Get");
    message.setArguments({screencast, "AvailableCursorModes"});
    const QDBusReply<QDBusVariant> reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 1000);
    return reply.isValid() ? reply.value().variant().toUInt() : 1u;
}
QVariantMap WaylandDisplayCapture::request(const QString &method, const QVariantList &arguments,
                                        QVariantMap options)
{
    const auto handle = token();
    auto sender = m_bus.baseService().mid(1);
    sender.replace('.', '_');
    m_request = "/org/freedesktop/portal/desktop/request/" + sender + "/" + handle;
    options.insert("handle_token", handle);
    m_results.clear();
    m_responded = false;
    if (!m_bus.connect(service, m_request, "org.freedesktop.portal.Request", "Response",
                       this, SLOT(response(uint,QVariantMap)))) {
        m_error = tr("无法监听系统屏幕共享响应。");
        return {};
    }
    const auto disconnect = qScopeGuard([&] {
        m_bus.disconnect(service, m_request, "org.freedesktop.portal.Request", "Response",
                         this, SLOT(response(uint,QVariantMap)));
        if (!m_responded)
            m_bus.asyncCall(call(m_request, "org.freedesktop.portal.Request", "Close"));
        m_request.clear();
    });
    auto message = call(path, screencast, method);
    auto args = arguments;
    args.append(options);
    message.setArguments(args);
    QDBusPendingCallWatcher watcher(m_bus.asyncCall(message, 5000));
    connect(&watcher, &QDBusPendingCallWatcher::finished, &m_loop, [&] {
        QDBusPendingReply<QDBusObjectPath> reply = watcher;
        if (reply.isError()) {
            m_error = tr("系统屏幕共享请求失败：%1").arg(reply.error().message());
            m_loop.quit();
        }
    });
    m_loop.exec();
    return m_results;
}
void WaylandDisplayCapture::response(uint code, const QVariantMap &results)
{
    m_responded = true;
    if (code != 0)
        m_error = code == 1 ? tr("已取消选择显示器。") : tr("系统拒绝或未能启动屏幕共享。");
    else
        m_results = results;
    m_loop.quit();
}
void WaylandDisplayCapture::closed()
{
    m_closed = true;
    m_error = tr("屏幕共享已停止，请手动重试。");
    m_loop.quit();
}
QString WaylandDisplayCapture::run(bool cursor, quint64 generation, std::atomic_bool &stop,
                                 LatestCaptureFrameSlot &frames)
{
    QTimer cancellation;
    connect(&cancellation, &QTimer::timeout, &m_loop, [&] {
        if (stop.load())
            m_loop.quit();
    });
    cancellation.start(20);
    if (stop.load())
        return {};
    if (!m_bus.isConnected())
        return tr("无法连接桌面会话总线，请在 Wayland 桌面内启动应用。");
    // Predict the session path as well, so cancellation can close a session even
    // when CreateSession's response has not yet reached us.
    const auto sessionToken = token();
    auto sender = m_bus.baseService().mid(1);
    sender.replace('.', '_');
    m_session = "/org/freedesktop/portal/desktop/session/" + sender + "/" + sessionToken;
    m_bus.connect(service, m_session, "org.freedesktop.portal.Session", "Closed",
                  this, SLOT(closed()));
    const auto created = request("CreateSession", {}, {{"session_handle_token", sessionToken}});
    if (stop.load() || !m_error.isEmpty())
        return m_error;
    if (created.value("session_handle").toString() != m_session)
        return tr("系统返回了无效的屏幕共享会话。");
    const uint modes = cursorModes();
    uint mode = 1; // Hidden cursor is the default.
    if (cursor && (modes & 2))
        mode = 2;
    else if (!(modes & 1))
        mode = (modes & 2) ? 2 : 4;
    request("SelectSources", {QVariant::fromValue(QDBusObjectPath(m_session))},
            {{"types", 1u}, {"multiple", false}, {"cursor_mode", mode}});
    if (stop.load() || !m_error.isEmpty())
        return m_error;
    const auto started = request("Start", {QVariant::fromValue(QDBusObjectPath(m_session)), QString()}, {});
    if (stop.load() || !m_error.isEmpty())
        return m_error;
    const auto streams = started.value("streams").value<QDBusArgument>();
    uint node = PW_ID_ANY;
    streams.beginArray();
    if (!streams.atEnd()) {
        QVariantMap properties;
        streams.beginStructure();
        streams >> node >> properties;
        streams.endStructure();
    }
    streams.endArray();
    if (node == PW_ID_ANY)
        return tr("系统未返回可用的显示器视频流。");
    auto open = call(path, screencast, "OpenPipeWireRemote");
    open.setArguments({QVariant::fromValue(QDBusObjectPath(m_session)), QVariantMap{}});
    const QDBusReply<QDBusUnixFileDescriptor> remote = m_bus.call(open, QDBus::Block, 5000);
    if (!remote.isValid())
        return tr("无法打开屏幕共享视频连接：%1").arg(remote.error().message());
    static std::once_flag init;
    std::call_once(init, [] { pw_init(nullptr, nullptr); });
    auto *loop = pw_main_loop_new(nullptr);
    if (!loop)
        return tr("无法创建 PipeWire 循环。");
    const auto destroyLoop = qScopeGuard([&] { pw_main_loop_destroy(loop); });
    auto *context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
    if (!context)
        return tr("无法创建 PipeWire 上下文。");
    const auto destroyContext = qScopeGuard([&] { pw_context_destroy(context); });
    const int fd = dup(remote.value().fileDescriptor());
    if (fd < 0)
        return tr("无法复制屏幕共享连接。");
    auto *core = pw_context_connect_fd(context, fd, nullptr, 0);
    if (!core)
        return tr("无法连接 PipeWire 屏幕共享服务。");
    const auto disconnectCore = qScopeGuard([&] { pw_core_disconnect(core); });
    StreamCapture capture;
    capture.frames = &frames;
    capture.generation = generation;
    pw_core_events coreEvents{};
    coreEvents.version = PW_VERSION_CORE_EVENTS;
    coreEvents.error = [](void *data, uint32_t, int, int, const char *message) {
        static_cast<StreamCapture *>(data)->error =
            QObject::tr("PipeWire 屏幕共享错误：%1").arg(QString::fromUtf8(message ? message : ""));
    };
    spa_hook coreListener{};
    pw_core_add_listener(core, &coreListener, &coreEvents, &capture);
    const auto removeCoreListener = qScopeGuard([&] { spa_hook_remove(&coreListener); });
    capture.stream = pw_stream_new(core, "OpenLive display capture",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    if (!capture.stream)
        return tr("无法创建显示器视频流。");
    const auto destroyStream = qScopeGuard([&] { pw_stream_destroy(capture.stream); });
    pw_stream_events events{};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.state_changed = StreamCapture::stateChanged;
    events.param_changed = StreamCapture::paramChanged;
    events.process = StreamCapture::process;
    spa_hook listener{};
    pw_stream_add_listener(capture.stream, &listener, &events, &capture);
    uint8_t storage[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    const spa_fraction rate{30, 1}, minimum{0, 1};
    const spa_pod *params[] = {static_cast<spa_pod *>(spa_pod_builder_add_object(
        &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_BGRx, SPA_VIDEO_FORMAT_BGRx,
            SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_RGBx, SPA_VIDEO_FORMAT_RGBA),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&rate, &minimum, &rate)))};
    if (pw_stream_connect(capture.stream, PW_DIRECTION_INPUT, node,
                          pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
                          params, 1) < 0)
        return tr("无法启动显示器视频流。");
    QTimer pump;
    const auto startTime = MediaClock::nowNs();
    connect(&pump, &QTimer::timeout, &m_loop, [&] {
        if (pw_loop_iterate(pw_main_loop_get_loop(loop), 0) < 0 && capture.error.isEmpty())
            capture.error = tr("PipeWire 视频连接失败。");
        if (!capture.error.isEmpty() || m_closed || stop.load())
            m_loop.quit();
        else if (!capture.sequence && MediaClock::nowNs() - startTime > 15000000000LL) {
            capture.error = tr("等待显示器首帧超时，请手动重试。");
            m_loop.quit();
        }
    });
    pump.start(5);
    if (!stop.load() && !m_closed)
        m_loop.exec();
    return !m_error.isEmpty() ? m_error : capture.error;
}
