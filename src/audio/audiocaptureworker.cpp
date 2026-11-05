#include "audio/audiocaptureworker.h"

#include <QScopeGuard>
#include <pulse/pulseaudio.h>
#include <cstring>

namespace {
QString deviceId(AudioSourceType type, const QString &name)
{
    return QStringLiteral("pulse:%1:%2").arg(toString(type), name);
}

struct CaptureDevice
{
    AudioDevice device;
    QString sourceName;
    uint32_t sourceIndex = PA_INVALID_INDEX;
};

// Context, queries and stream callbacks all run on the owning worker thread.
class AudioSession
{
public:
    ~AudioSession()
    {
        closeStream();
        if (context) {
            pa_context_set_subscribe_callback(context, nullptr, nullptr);
            pa_context_disconnect(context);
            pa_context_unref(context);
        }
        if (loop)
            pa_mainloop_free(loop);
    }

    bool open()
    {
        loop = pa_mainloop_new();
        if (loop)
            context = pa_context_new(pa_mainloop_get_api(loop), "OpenLive Studio");
        if (!context) {
            error = QStringLiteral("无法创建 PulseAudio 音频连接。");
            return false;
        }
        if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
            fail(QStringLiteral("无法连接 PulseAudio 音频服务"));
            return false;
        }
        const auto deadline = audioClockNs() + 3000000000LL;
        while (pa_context_get_state(context) != PA_CONTEXT_READY && audioClockNs() < deadline &&
               iterate()) {
        }
        if (pa_context_get_state(context) != PA_CONTEXT_READY) {
            if (error.isEmpty())
                error = QStringLiteral("连接 PulseAudio 音频服务超时，请检查桌面音频服务。");
            return false;
        }
        pa_context_set_subscribe_callback(
            context,
            [](pa_context *, pa_subscription_event_type_t, uint32_t, void *data) {
                static_cast<AudioSession *>(data)->catalogDirty = true;
            },
            this);
        auto *operation = pa_context_subscribe(
            context,
            pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SERVER | PA_SUBSCRIPTION_MASK_SOURCE |
                                   PA_SUBSCRIPTION_MASK_SINK),
            [](pa_context *, int success, void *data) {
                if (!success)
                    static_cast<AudioSession *>(data)->fail(QStringLiteral("无法监听音频设备变化"));
            },
            this);
        releaseOperation(operation);
        return error.isEmpty() && refresh();
    }

    bool iterate()
    {
        if (QThread::currentThread()->isInterruptionRequested())
            return false;
        if (pa_mainloop_prepare(loop, 20) < 0 || pa_mainloop_poll(loop) < 0 ||
            pa_mainloop_dispatch(loop) < 0) {
            error = QStringLiteral("PulseAudio 音频事件循环失败。");
            return false;
        }
        const auto state = pa_context_get_state(context);
        if (state == PA_CONTEXT_FAILED || state == PA_CONTEXT_TERMINATED)
            fail(QStringLiteral("PulseAudio 音频服务已断开"));
        return error.isEmpty();
    }

    bool refresh()
    {
        catalogDirty = false;
        pendingDevices.clear();
        remainingQueries = 3;
        releaseOperation(pa_context_get_server_info(
            context,
            [](pa_context *, const pa_server_info *info, void *data) {
                auto &self = *static_cast<AudioSession *>(data);
                if (info) {
                    self.defaultSource = QString::fromUtf8(info->default_source_name);
                    self.defaultSink = QString::fromUtf8(info->default_sink_name);
                } else
                    self.fail(QStringLiteral("无法读取默认音频设备"));
                --self.remainingQueries;
            },
            this));
        releaseOperation(pa_context_get_source_info_list(
            context,
            [](pa_context *, const pa_source_info *info, int end, void *data) {
                auto &self = *static_cast<AudioSession *>(data);
                if (end) {
                    if (end < 0)
                        self.fail(QStringLiteral("无法枚举麦克风"));
                    --self.remainingQueries;
                } else if (info && info->name && info->monitor_of_sink == PA_INVALID_INDEX) {
                    const auto name = QString::fromUtf8(info->name);
                    self.pendingDevices.append(
                        {{deviceId(AudioSourceType::Microphone, name),
                          QString::fromUtf8(info->description ? info->description : info->name),
                          AudioSourceType::Microphone},
                         name,
                         info->index});
                }
            },
            this));
        releaseOperation(pa_context_get_sink_info_list(
            context,
            [](pa_context *, const pa_sink_info *info, int end, void *data) {
                auto &self = *static_cast<AudioSession *>(data);
                if (end) {
                    if (end < 0)
                        self.fail(QStringLiteral("无法枚举输出设备"));
                    --self.remainingQueries;
                } else if (info && info->name && info->monitor_source_name &&
                           info->monitor_source != PA_INVALID_INDEX) {
                    const auto name = QString::fromUtf8(info->name);
                    self.pendingDevices.append(
                        {{deviceId(AudioSourceType::SystemAudio, name),
                          QString::fromUtf8(info->description ? info->description : info->name),
                          AudioSourceType::SystemAudio},
                         QString::fromUtf8(info->monitor_source_name),
                         info->monitor_source});
                }
            },
            this));
        const auto deadline = audioClockNs() + 2000000000LL;
        while (remainingQueries && error.isEmpty() && audioClockNs() < deadline && iterate()) {
        }
        if (remainingQueries && error.isEmpty())
            error = QStringLiteral("查询 PulseAudio 音频设备超时。");
        if (!error.isEmpty())
            return false;
        devices = std::move(pendingDevices);
        return true;
    }

    CaptureDevice target(const QString &id, AudioSourceType type) const
    {
        const bool followDefault = id == defaultAudioDeviceId(type);
        const auto desiredId =
            followDefault
                ? deviceId(type, type == AudioSourceType::Microphone ? defaultSource : defaultSink)
                : id;
        for (const auto &entry : devices)
            if (entry.device.type == type && entry.device.id == desiredId)
                return entry;
        return {};
    }

    void startStream(const CaptureDevice &device, AudioPacketQueue *destination)
    {
        queue = destination;
        streamError.clear();
        const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, AudioFormat::rate, AudioFormat::channels};
        pa_channel_map channels;
        pa_channel_map_init_stereo(&channels);
        stream = pa_stream_new(context, "OpenLive audio capture", &format, &channels);
        if (!stream) {
            streamError = message(QStringLiteral("无法创建音频采集流"));
            return;
        }
        pa_stream_set_state_callback(
            stream,
            [](pa_stream *stream, void *data) {
                auto &self = *static_cast<AudioSession *>(data);
                const auto state = pa_stream_get_state(stream);
                if (state == PA_STREAM_FAILED || state == PA_STREAM_TERMINATED)
                    self.streamError = self.message(QStringLiteral("音频设备采集已断开"));
            },
            this);
        pa_stream_set_read_callback(
            stream,
            [](pa_stream *, size_t, void *data) {
                static_cast<AudioSession *>(data)->read();
            },
            this);
        pa_stream_set_overflow_callback(
            stream,
            [](pa_stream *, void *data) {
                auto &self = *static_cast<AudioSession *>(data);
                self.queue->clear();
                self.nextTimestamp = 0;
            },
            this);
        pa_buffer_attr buffer{};
        buffer.maxlength = uint32_t(pa_usec_to_bytes(100000, &format));
        buffer.fragsize = uint32_t(pa_usec_to_bytes(10000, &format));
        buffer.tlength = buffer.prebuf = buffer.minreq = uint32_t(-1);
        const auto sourceName = device.sourceName.toUtf8();
        // Resolve defaults ourselves; fixed devices must never follow server fallback policy.
        if (pa_stream_connect_record(
                stream, sourceName.constData(), &buffer,
                pa_stream_flags_t(PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE |
                                  PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_DONT_MOVE)) < 0)
            streamError = message(QStringLiteral("无法启动音频采集"));
    }

    void closeStream()
    {
        if (stream) {
            pa_stream_set_state_callback(stream, nullptr, nullptr);
            pa_stream_set_read_callback(stream, nullptr, nullptr);
            pa_stream_set_overflow_callback(stream, nullptr, nullptr);
            pa_stream_disconnect(stream);
            pa_stream_unref(stream);
            stream = nullptr;
        }
        nextTimestamp = lastPacketNs = 0;
        if (queue)
            queue->clear();
    }

    QList<CaptureDevice> devices;
    QString error, streamError;
    pa_stream *stream = nullptr;
    bool catalogDirty = true;
    qint64 lastPacketNs = 0;

private:
    QString message(const QString &operation) const
    {
        return operation + QStringLiteral("：%1").arg(
                               QString::fromUtf8(pa_strerror(pa_context_errno(context))));
    }

    void fail(const QString &operation)
    {
        error = message(operation);
    }

    void releaseOperation(pa_operation *operation)
    {
        if (operation)
            pa_operation_unref(operation);
        else
            fail(QStringLiteral("无法查询或订阅 PulseAudio 设备"));
    }

    void read()
    {
        while (true) {
            const void *data = nullptr;
            size_t bytes = 0;
            if (pa_stream_peek(stream, &data, &bytes) < 0) {
                streamError = message(QStringLiteral("无法读取音频采样"));
                return;
            }
            if (!bytes)
                return;
            const auto drop = qScopeGuard([&] {
                if (pa_stream_drop(stream) < 0)
                    streamError = message(QStringLiteral("无法归还音频缓冲"));
            });
            constexpr size_t frameBytes = sizeof(float) * AudioFormat::channels;
            if (bytes % frameBytes || bytes / frameBytes > size_t(AudioPacketQueue::maxFrames)) {
                streamError = QStringLiteral("PulseAudio 返回了无效的音频缓冲。");
                return;
            }
            const auto now = audioClockNs();
            lastPacketNs = now;
            const auto duration = AudioFormat::framesToNs(qint64(bytes / frameBytes));
            qint64 timestamp = now - duration;
            pa_usec_t latency = 0;
            int negative = 0;
            // A sink monitor can report negative latency for audio queued for playback.
            // Anchor that available audio to arrival time instead of publishing far ahead.
            if (pa_stream_get_latency(stream, &latency, &negative) >= 0 && !negative &&
                latency <= 1000000)
                timestamp = now - qint64(latency) * 1000;
            if (!nextTimestamp || std::abs(timestamp - nextTimestamp) > 50000000LL) {
                queue->clear();
                nextTimestamp = timestamp;
            }
            AudioBlock block;
            block.timestampNs = nextTimestamp;
            block.samples.fill(0, qsizetype(bytes / sizeof(float)));
            // A PulseAudio hole has a nonzero length and null data: publish silence.
            if (data)
                memcpy(block.samples.data(), data, bytes);
            for (auto &sample : block.samples)
                if (!std::isfinite(sample))
                    sample = 0;
            nextTimestamp += duration;
            queue->publish(std::move(block));
        }
    }

    pa_mainloop *loop = nullptr;
    pa_context *context = nullptr;
    QList<CaptureDevice> pendingDevices;
    QString defaultSource, defaultSink;
    int remainingQueries = 0;
    AudioPacketQueue *queue = nullptr;
    qint64 nextTimestamp = 0;
};
}

QString defaultAudioDeviceId(AudioSourceType type)
{
    return QStringLiteral("pulse:default:%1").arg(toString(type));
}

QList<AudioDevice> audioDevices(QString *error)
{
    AudioSession session;
    if (!session.open()) {
        if (error)
            *error = session.error;
        return {};
    }
    QList<AudioDevice> result;
    for (const auto type : {AudioSourceType::Microphone, AudioSourceType::SystemAudio}) {
        if (std::any_of(session.devices.cbegin(), session.devices.cend(), [=](const auto &entry) {
                return entry.device.type == type;
            }))
            result.append({defaultAudioDeviceId(type),
                           type == AudioSourceType::Microphone
                               ? QStringLiteral("跟随系统默认输入设备")
                               : QStringLiteral("跟随系统默认输出设备"),
                           type});
    }
    for (const auto &entry : session.devices)
        result.append(entry.device);
    if (error)
        *error = result.isEmpty() ? QStringLiteral("PulseAudio 未发现可用的音频输入或输出设备。")
                                  : QString();
    return result;
}

AudioCaptureWorker::AudioCaptureWorker(QString deviceId, AudioSourceType type,
                                       std::shared_ptr<AudioPacketQueue> queue)
    : m_deviceId(std::move(deviceId)), m_type(type), m_queue(std::move(queue))
{
    setObjectName(QStringLiteral("AudioCapture"));
}

AudioCaptureWorker::~AudioCaptureWorker()
{
    stop();
    wait();
}

void AudioCaptureWorker::stop()
{
    requestInterruption();
}

AudioCaptureWorker::Status AudioCaptureWorker::status() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_status;
}

void AudioCaptureWorker::setStatus(const QString &state, const QString &error)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_status = {state, error};
}

void AudioCaptureWorker::run()
{
    while (!isInterruptionRequested()) {
        {
            AudioSession session;
            if (session.open()) {
                uint32_t activeIndex = PA_INVALID_INDEX;
                QString activeName;
                qint64 retryAt = 0, connectingSince = 0;
                while (!isInterruptionRequested() && session.iterate()) {
                    if (session.catalogDirty && !session.refresh())
                        break;
                    const auto now = audioClockNs();
                    const auto target = session.target(m_deviceId, m_type);
                    const bool forcedFailure =
                        qEnvironmentVariable("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE") == m_deviceId;
                    if (target.sourceIndex != activeIndex || target.sourceName != activeName ||
                        target.sourceIndex == PA_INVALID_INDEX || forcedFailure) {
                        session.closeStream();
                        if (target.sourceIndex != activeIndex || target.sourceName != activeName)
                            retryAt = 0;
                        activeIndex = target.sourceIndex;
                        activeName = target.sourceName;
                        if (target.sourceIndex == PA_INVALID_INDEX || forcedFailure) {
                            setStatus(
                                "error",
                                forcedFailure
                                    ? QStringLiteral("音频设备测试故障。")
                                    : QStringLiteral(
                                          "PulseAudio 中未找到所选音频设备，正在等待设备恢复。"));
                            continue;
                        }
                        setStatus("starting");
                    }
                    if (!session.stream && now >= retryAt) {
                        session.startStream(target, m_queue.get());
                        connectingSince = now;
                    }
                    if (session.stream && pa_stream_get_state(session.stream) == PA_STREAM_READY)
                        setStatus("capturing");
                    else if (session.stream && now - connectingSince > 5000000000LL &&
                             session.streamError.isEmpty())
                        session.streamError = QStringLiteral("等待音频设备连接超时，将自动重试。");
                    if (session.stream && m_type == AudioSourceType::Microphone &&
                        now - std::max(connectingSince, session.lastPacketNs) > 3000000000LL &&
                        session.streamError.isEmpty())
                        session.streamError = QStringLiteral("麦克风采样中断，将自动重新连接。");
                    if (!session.streamError.isEmpty()) {
                        setStatus("error", session.streamError);
                        session.closeStream();
                        session.streamError.clear();
                        retryAt = now + 1000000000LL;
                    }
                }
            }
            if (!isInterruptionRequested())
                setStatus("error", session.error);
        }
        m_queue->clear();
        for (int i = 0; i < 10 && !isInterruptionRequested(); ++i)
            msleep(100);
    }
    m_queue->clear();
    setStatus("stopped");
}
