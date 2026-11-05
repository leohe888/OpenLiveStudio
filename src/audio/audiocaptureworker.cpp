#include "audio/audiocaptureworker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QScopeGuard>
#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>
#include <spa/param/audio/format-utils.h>
#include <spa/buffer/meta.h>
#include <cerrno>
#include <cstring>

namespace {
QString nodeDeviceId(AudioSourceType type, const QString &name)
{
    return QStringLiteral("pipewire:%1:%2").arg(toString(type), name);
}

struct AudioNode
{
    QString name;
    QString description;
    AudioSourceType type = AudioSourceType::Microphone;
};

// Each instance belongs entirely to the querying/capture thread. Process callbacks
// run on this loop too (no RT_PROCESS), so Qt allocations and the queue are safe.
class AudioSession
{
public:
    ~AudioSession()
    {
        closeStream();
        if (metadata) {
            spa_hook_remove(&metadataListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(metadata));
        }
        if (registry) {
            spa_hook_remove(&registryListener);
            pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
        }
        if (core) {
            spa_hook_remove(&coreListener);
            pw_core_disconnect(core);
        }
        if (context)
            pw_context_destroy(context);
        if (loop)
            pw_main_loop_destroy(loop);
    }

    bool open()
    {
        static std::once_flag init;
        std::call_once(init, [] { pw_init(nullptr, nullptr); });
        loop = pw_main_loop_new(nullptr);
        if (loop)
            context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
        if (context)
            core = pw_context_connect(context, pw_properties_new(
                PW_KEY_APP_NAME, "OpenLive Studio", nullptr), 0);
        if (!core) {
            error = QStringLiteral("无法连接 PipeWire 音频服务：%1").arg(
                QString::fromLocal8Bit(strerror(errno)));
            return false;
        }
        coreEvents.version = PW_VERSION_CORE_EVENTS;
        coreEvents.error = [](void *data, uint32_t id, int, int, const char *message) {
            auto &self = *static_cast<AudioSession *>(data);
            const auto text = QStringLiteral("PipeWire 音频连接错误：%1").arg(
                QString::fromUtf8(message ? message : ""));
            if (id == PW_ID_CORE)
                self.error = text;
            else
                self.streamError = text;
        };
        coreEvents.done = [](void *data, uint32_t id, int seq) {
            auto &self = *static_cast<AudioSession *>(data);
            if (id == PW_ID_CORE && seq == self.syncSequence)
                self.synced = true;
        };
        pw_core_add_listener(core, &coreListener, &coreEvents, this);
        registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
        if (!registry) {
            error = QStringLiteral("无法查询 PipeWire 音频设备。");
            return false;
        }
        registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
        registryEvents.global = [](void *data, uint32_t id, uint32_t, const char *type,
                                   uint32_t version, const spa_dict *props) {
            auto &self = *static_cast<AudioSession *>(data);
            if (!props)
                return;
            if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
                const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
                const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
                if (!mediaClass || !name)
                    return;
                AudioSourceType sourceType;
                if (strcmp(mediaClass, "Audio/Source") == 0 ||
                    strcmp(mediaClass, "Audio/Source/Virtual") == 0)
                    sourceType = AudioSourceType::Microphone;
                else if (strcmp(mediaClass, "Audio/Sink") == 0)
                    sourceType = AudioSourceType::SystemAudio;
                else
                    return;
                const char *description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
                self.nodes.insert(id, {QString::fromUtf8(name),
                    QString::fromUtf8(description ? description : name), sourceType});
            } else if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0 && !self.metadata) {
                const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
                if (!name || strcmp(name, "default") != 0)
                    return;
                self.metadata = static_cast<pw_metadata *>(pw_registry_bind(self.registry,
                    id, type, std::min(version, uint32_t(PW_VERSION_METADATA)), 0));
                if (!self.metadata)
                    return;
                self.metadataId = id;
                self.metadataEvents.version = PW_VERSION_METADATA_EVENTS;
                self.metadataEvents.property = [](void *data, uint32_t subject, const char *key,
                                                   const char *, const char *value) {
                    auto &session = *static_cast<AudioSession *>(data);
                    if (subject != PW_ID_CORE)
                        return 0;
                    if (!key) {
                        session.defaultSource.clear();
                        session.defaultSink.clear();
                    } else {
                        const auto name = QJsonDocument::fromJson(value ? value : "null")
                                              .object().value("name").toString();
                        if (strcmp(key, "default.audio.source") == 0)
                            session.defaultSource = name;
                        else if (strcmp(key, "default.audio.sink") == 0)
                            session.defaultSink = name;
                    }
                    return 0;
                };
                pw_metadata_add_listener(self.metadata, &self.metadataListener,
                                         &self.metadataEvents, &self);
            }
        };
        registryEvents.global_remove = [](void *data, uint32_t id) {
            auto &self = *static_cast<AudioSession *>(data);
            self.nodes.remove(id);
            if (self.metadata && id == self.metadataId) {
                spa_hook_remove(&self.metadataListener);
                pw_proxy_destroy(reinterpret_cast<pw_proxy *>(self.metadata));
                self.metadata = nullptr;
                self.defaultSource.clear();
                self.defaultSink.clear();
            }
        };
        pw_registry_add_listener(registry, &registryListener, &registryEvents, this);
        return true;
    }

    bool iterate(int timeoutMs)
    {
        if (pw_loop_iterate(pw_main_loop_get_loop(loop), timeoutMs) < 0 && error.isEmpty())
            error = QStringLiteral("PipeWire 音频事件循环失败。");
        return error.isEmpty();
    }

    bool roundtrip()
    {
        synced = false;
        syncSequence = pw_core_sync(core, PW_ID_CORE, syncSequence);
        const auto deadline = audioClockNs() + 2000000000LL;
        while (!synced && error.isEmpty() && audioClockNs() < deadline)
            iterate(20);
        if (!synced && error.isEmpty())
            error = QStringLiteral("查询 PipeWire 音频设备超时。");
        return error.isEmpty();
    }

    uint32_t target(const QString &deviceId, AudioSourceType type) const
    {
        const bool followDefault = deviceId == defaultAudioDeviceId(type);
        const auto defaultName = type == AudioSourceType::Microphone ? defaultSource : defaultSink;
        for (auto it = nodes.cbegin(); it != nodes.cend(); ++it) {
            if (it->type == type && (followDefault ? it->name == defaultName
                                                  : nodeDeviceId(type, it->name) == deviceId))
                return it.key();
        }
        return PW_ID_ANY;
    }

    bool startStream(uint32_t targetId, AudioPacketQueue *destination)
    {
        queue = destination;
        streamError.clear();
        const auto node = nodes.value(targetId);
        const auto targetName = node.name.toUtf8();
        // PipeWire 0.3.48 media-session still routes streams via node.target.
        const auto legacyTarget = QByteArray::number(targetId);
        auto *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Production",
            PW_KEY_TARGET_OBJECT, targetName.constData(),
            PW_KEY_NODE_TARGET, legacyTarget.constData(),
            PW_KEY_NODE_DONT_RECONNECT, "true", "node.dont-fallback", "true",
            "node.dont-move", "true", "state.restore-props", "false",
            PW_KEY_NODE_LATENCY, "480/48000",
            PW_KEY_NODE_WANT_DRIVER, "true",
            PW_KEY_STREAM_CAPTURE_SINK, node.type == AudioSourceType::SystemAudio ? "true" : "false",
            nullptr);
        stream = pw_stream_new(core, "OpenLive audio capture", props);
        if (!stream) {
            streamError = QStringLiteral("无法创建 PipeWire 音频流。");
            return false;
        }
        streamEvents = {};
        streamEvents.version = PW_VERSION_STREAM_EVENTS;
        streamEvents.state_changed = [](void *data, pw_stream_state, pw_stream_state state,
                                        const char *message) {
            auto &self = *static_cast<AudioSession *>(data);
            self.streamState = state;
            if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
                self.streamError = QStringLiteral("音频设备已断开：%1").arg(
                    QString::fromUtf8(message ? message : ""));
        };
        streamEvents.param_changed = [](void *data, uint32_t id, const spa_pod *param) {
            auto &self = *static_cast<AudioSession *>(data);
            if (id != SPA_PARAM_Format || !param)
                return;
            spa_audio_info_raw format{};
            self.formatReady = spa_format_audio_raw_parse(param, &format) >= 0 &&
                format.format == SPA_AUDIO_FORMAT_F32 && format.rate == AudioFormat::rate &&
                format.channels == AudioFormat::channels;
            if (!self.formatReady)
                self.streamError = QStringLiteral("PipeWire 未提供 48 kHz 双声道浮点音频。");
        };
        streamEvents.process = [](void *data) { static_cast<AudioSession *>(data)->process(); };
        pw_stream_add_listener(stream, &streamListener, &streamEvents, this);
        uint8_t storage[512];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
        spa_audio_info_raw format{};
        format.format = SPA_AUDIO_FORMAT_F32;
        format.rate = AudioFormat::rate;
        format.channels = AudioFormat::channels;
        format.position[0] = SPA_AUDIO_CHANNEL_FL;
        format.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *params[]{spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
        const int result = pw_stream_connect(stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
        if (result < 0)
            streamError = QStringLiteral("无法启动音频采集：%1").arg(
                QString::fromLocal8Bit(strerror(-result)));
        return streamError.isEmpty();
    }

    void closeStream()
    {
        if (stream) {
            spa_hook_remove(&streamListener);
            pw_stream_destroy(stream);
            stream = nullptr;
        }
        streamState = PW_STREAM_STATE_UNCONNECTED;
        formatReady = false;
        nextTimestamp = 0;
        lastPacketNs = 0;
        if (queue)
            queue->clear();
    }

    QString error, streamError;
    QMap<uint32_t, AudioNode> nodes;
    pw_stream *stream = nullptr;
    pw_stream_state streamState = PW_STREAM_STATE_UNCONNECTED;
    qint64 lastPacketNs = 0;

private:
    void process()
    {
        while (auto *buffer = pw_stream_dequeue_buffer(stream)) {
            const auto recycle = qScopeGuard([&] { pw_stream_queue_buffer(stream, buffer); });
            const auto *buf = buffer->buffer;
            if (!formatReady || !buf || buf->n_datas != 1)
                continue;
            const auto &data = buf->datas[0];
            if (!data.data || !data.chunk || !data.maxsize ||
                (data.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
                continue;
            constexpr uint32_t frameBytes = sizeof(float) * AudioFormat::channels;
            const auto size = data.chunk->size;
            if (!size)
                continue;
            if (size > data.maxsize || size % frameBytes ||
                size / frameBytes > uint32_t(AudioPacketQueue::maxFrames) ||
                (data.chunk->stride != 0 && data.chunk->stride != int32_t(frameBytes))) {
                streamError = QStringLiteral("PipeWire 返回了无效的音频缓冲。");
                return;
            }
            const auto *header = static_cast<const spa_meta_header *>(
                spa_buffer_find_meta_data(buf, SPA_META_Header, sizeof(spa_meta_header)));
            if (header && (header->flags & SPA_META_HEADER_FLAG_CORRUPTED))
                continue;
            const auto duration = AudioFormat::framesToNs(size / frameBytes);
            const auto now = audioClockNs();
            lastPacketNs = now;
            qint64 timestamp = now - duration;
            pw_time time{};
            if (pw_stream_get_time(stream, &time) >= 0 && time.now > 0 && time.rate.denom) {
                // PipeWire's graph clock uses CLOCK_MONOTONIC, like MediaClock.
                timestamp = time.now - qint64(double(time.delay) * time.rate.num *
                    1000000000.0 / time.rate.denom) -
                    AudioFormat::framesToNs(qint64(time.queued)) - duration;
            }
            if (header && header->pts > now - 1000000000LL && header->pts <= now)
                timestamp = header->pts;
            if (timestamp < now - 1000000000LL || timestamp > now)
                timestamp = now - duration;
            if (!nextTimestamp || std::abs(timestamp - nextTimestamp) > 50000000LL ||
                (header && (header->flags & SPA_META_HEADER_FLAG_DISCONT))) {
                queue->clear();
                nextTimestamp = timestamp;
            }
            AudioBlock block;
            block.timestampNs = nextTimestamp;
            block.samples.resize(size / sizeof(float));
            const auto offset = data.chunk->offset % data.maxsize;
            const auto first = std::min(size, data.maxsize - offset);
            const auto *bytes = static_cast<const uint8_t *>(data.data);
            memcpy(block.samples.data(), bytes + offset, first);
            if (first < size)
                memcpy(reinterpret_cast<uint8_t *>(block.samples.data()) + first, bytes, size - first);
            for (auto &sample : block.samples)
                if (!std::isfinite(sample))
                    sample = 0;
            nextTimestamp += duration;
            queue->publish(std::move(block));
        }
    }

    pw_main_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    pw_metadata *metadata = nullptr;
    uint32_t metadataId = PW_ID_ANY;
    spa_hook coreListener{}, registryListener{}, metadataListener{}, streamListener{};
    pw_core_events coreEvents{};
    pw_registry_events registryEvents{};
    pw_metadata_events metadataEvents{};
    pw_stream_events streamEvents{};
    int syncSequence = 0;
    bool synced = false, formatReady = false;
    QString defaultSource, defaultSink;
    AudioPacketQueue *queue = nullptr;
    qint64 nextTimestamp = 0;
};
}

QString defaultAudioDeviceId(AudioSourceType type)
{
    return QStringLiteral("pipewire:default:%1").arg(toString(type));
}

QList<AudioDevice> audioDevices(QString *error)
{
    AudioSession session;
    // The second roundtrip includes the default metadata bound during the first.
    if (!session.open() || !session.roundtrip() || !session.roundtrip()) {
        if (error)
            *error = session.error;
        return {};
    }
    if (error)
        error->clear();
    QList<AudioDevice> result{
        {defaultAudioDeviceId(AudioSourceType::Microphone),
         QStringLiteral("跟随系统默认输入设备"), AudioSourceType::Microphone},
        {defaultAudioDeviceId(AudioSourceType::SystemAudio),
         QStringLiteral("跟随系统默认输出设备"), AudioSourceType::SystemAudio}};
    for (const auto &node : session.nodes)
        result.append({nodeDeviceId(node.type, node.name), node.description, node.type});
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
                uint32_t activeTarget = PW_ID_ANY;
                qint64 retryAt = 0, connectingSince = 0;
                while (!isInterruptionRequested() && session.iterate(20)) {
                    const auto now = audioClockNs();
                    const auto target = session.target(m_deviceId, m_type);
                    const bool forcedFailure =
                        qEnvironmentVariable("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE") == m_deviceId;
                    if (target != activeTarget || target == PW_ID_ANY || forcedFailure) {
                        session.closeStream();
                        if (target != activeTarget)
                            retryAt = 0;
                        activeTarget = target;
                        if (target == PW_ID_ANY || forcedFailure) {
                            setStatus("error", forcedFailure ? QStringLiteral("音频设备测试故障。")
                                : QStringLiteral("音频设备不可用，正在等待设备恢复。"));
                            continue;
                        }
                        setStatus("starting");
                    }
                    if (!session.stream && now >= retryAt) {
                        session.startStream(target, m_queue.get());
                        connectingSince = now;
                    }
                    if (session.streamState == PW_STREAM_STATE_STREAMING)
                        setStatus("capturing");
                    else if (session.stream && now - connectingSince > 5000000000LL &&
                             session.streamError.isEmpty())
                        session.streamError = QStringLiteral("等待音频设备连接超时，将自动重试。");
                    // A live microphone produces PCM even in silence. STREAMING alone
                    // does not prove the graph is still delivering data after hotplug.
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
