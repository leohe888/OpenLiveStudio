#include "output/mediaencoder.h"
#include <QScopeGuard>
#include <cerrno>
#include "util/mediaclock.h"
#include <mutex>
#include <QDebug>
#include <cstdarg>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace {
thread_local bool rejectedStream = false;

void check(int result, const char *operation)
{
    if (result >= 0)
        return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw QStringLiteral("%1：%2").arg(QString::fromUtf8(operation), QString::fromUtf8(message));
}
}

MediaEncoder::~MediaEncoder()
{
    sws_freeContext(m_pixelConversionContext);
    av_frame_free(&m_videoFrame);
    av_frame_free(&m_audioFrame);
    avcodec_free_context(&m_videoCodecContext);
    avcodec_free_context(&m_audioCodecContext);
    if (m_network && m_format && m_format->pb) {
        resetNetworkDeadline();
        avio_closep(&m_format->pb);
    }
    if (m_format)
        m_format->pb = nullptr;
    avformat_free_context(m_format);
    if (m_io) {
        av_freep(&m_io->buffer);
        avio_context_free(&m_io);
    }
    m_file.close();
    // 只删除本次新建且尚无视频帧的文件；已写入内容的未完成录制应保留，供用户恢复。
    if (m_created && !m_videoFrames)
        m_file.remove();
}

int MediaEncoder::write(void *opaque, const unsigned char *data, int size)
{
    auto *self = static_cast<MediaEncoder *>(opaque);
    const auto count = self->m_file.write(reinterpret_cast<const char *>(data), size);
    return count == size ? size : AVERROR(EIO);
}

std::int64_t MediaEncoder::seek(void *opaque, std::int64_t offset, int whence)
{
    auto &file = static_cast<MediaEncoder *>(opaque)->m_file;
    if (whence == AVSEEK_SIZE)
        return file.size();
    whence &= ~AVSEEK_FORCE;
    const qint64 position = whence == SEEK_SET   ? offset
                            : whence == SEEK_CUR ? file.pos() + offset
                            : whence == SEEK_END ? file.size() + offset
                                                 : -1;
    return position >= 0 && file.seek(position) ? file.pos() : AVERROR(EIO);
}

void MediaEncoder::open(const QString &path, int bitrateKbps)
{
    if (m_created || bitrateKbps < 500 || bitrateKbps > 50000)
        throw QStringLiteral("录制编码参数无效。");
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw QStringLiteral("无法创建录制文件：%1").arg(m_file.errorString());
    m_created = true;
    check(avformat_alloc_output_context2(&m_format, nullptr, "mp4", nullptr),
          "无法创建 MP4 封装器");
    if (!m_format)
        throw QStringLiteral("无法分配 MP4 封装器。");
    auto *buffer = static_cast<unsigned char *>(av_malloc(65536));
    if (!buffer)
        throw QStringLiteral("无法分配录制写入缓冲。");
    m_io = avio_alloc_context(buffer, 65536, 1, this, nullptr, &MediaEncoder::write,
                              &MediaEncoder::seek);
    if (!m_io) {
        av_free(buffer);
        throw QStringLiteral("无法创建录制文件输出。");
    }
    m_io->seekable = AVIO_SEEKABLE_NORMAL;
    m_format->pb = m_io;
    m_format->flags |= AVFMT_FLAG_CUSTOM_IO;

    initialize(bitrateKbps);
    check(avformat_write_header(m_format, nullptr), "无法写入 MP4 文件头");
}

void MediaEncoder::resetNetworkDeadline()
{
    m_deadlineNs = MediaClock::nowNs() + 3000000000LL;
}

int MediaEncoder::interrupted(void *opaque)
{
    auto *self = static_cast<MediaEncoder *>(opaque);
    return (self->m_cancelled && self->m_cancelled()) || MediaClock::nowNs() > self->m_deadlineNs;
}

void MediaEncoder::networkCheck(int result, const char *operation)
{
    if (result >= 0)
        return;
    // RTMP 库的错误文本可能含串流密钥，只向上层返回操作名和错误码。
    const bool permanent = result == AVERROR(EACCES) || result == AVERROR(EPERM) ||
                           result == AVERROR(EINVAL) || result == AVERROR(ENOENT) ||
                           result == AVERROR_PROTOCOL_NOT_FOUND || result == AVERROR(ENOSYS) ||
                           rejectedStream;
    throw StreamFailure{
        QStringLiteral("%1（错误码 %2）").arg(QString::fromUtf8(operation)).arg(result),
        !permanent};
}

void MediaEncoder::openStream(const QString &url, int bitrateKbps, std::function<bool()> cancelled,
                              const QString &tcUrl)
{
    m_network = true;
    m_cancelled = std::move(cancelled);
    rejectedStream = false;
    // FFmpeg 原始日志可能包含服务器认证响应；全局日志回调统一脱敏，避免其他库线程泄漏密钥。
    static std::once_flag logging;
    std::call_once(logging, [] {
        av_log_set_callback([](void *, int level, const char *format, va_list args) {
            char message[2048]{};
            vsnprintf(message, sizeof(message), format, args);
            const auto text = QByteArray(message).toLower();
            if (text.contains("netstream.publish") && !text.contains("start"))
                rejectedStream = true;
            if (text.contains("server error") || text.contains("authentication") ||
                text.contains("access denied") || text.contains("badname") ||
                text.contains("publish.denied"))
                rejectedStream = true;
            if (level <= AV_LOG_ERROR)
                qWarning("FFmpeg reported a media error; see output status for its code.");
        });
    });
    check(avformat_alloc_output_context2(&m_format, nullptr, "flv", nullptr), "无法创建直播封装器");
    if (!m_format)
        throw QStringLiteral("无法分配直播封装器。");
    m_format->interrupt_callback = {&MediaEncoder::interrupted, this};
    m_format->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    initialize(bitrateKbps);
    AVDictionary *options = nullptr;
    av_dict_set(&options, "rw_timeout", "3000000", 0);
    av_dict_set(&options, "tcp_nodelay", "1", 0);
    if (!tcUrl.isEmpty())
        av_dict_set(&options, "rtmp_tcurl", tcUrl.toUtf8().constData(), 0);
    resetNetworkDeadline();
    const auto endpoint = url.toUtf8();
    const int opened = avio_open2(&m_format->pb, endpoint.constData(), AVIO_FLAG_WRITE,
                                  &m_format->interrupt_callback, &options);
    av_dict_free(&options);
    networkCheck(opened, "无法连接推流服务器");
    av_dict_set(&options, "flvflags", "no_duration_filesize", 0);
    resetNetworkDeadline();
    const int header = avformat_write_header(m_format, &options);
    av_dict_free(&options);
    networkCheck(header, "推流服务器未接受直播输出");
}

qint64 MediaEncoder::bytesSent() const
{
    return m_network && m_format && m_format->pb ? m_format->pb->bytes_written : 0;
}

void MediaEncoder::initialize(int bitrateKbps)
{
    if (bitrateKbps < 500 || bitrateKbps > 50000)
        throw QStringLiteral("视频码率无效。");
    const auto *videoCodec = avcodec_find_encoder_by_name("libx264");
    const auto *audioCodec = avcodec_find_encoder_by_name("aac");
    if (!videoCodec || !audioCodec)
        throw QStringLiteral("FFmpeg 缺少 libx264 或 AAC 编码器。");
    m_videoCodecContext = avcodec_alloc_context3(videoCodec);
    m_audioCodecContext = avcodec_alloc_context3(audioCodec);
    if (!m_videoCodecContext || !m_audioCodecContext)
        throw QStringLiteral("无法分配音视频编码器。");
    m_videoCodecContext->width = 1920;
    m_videoCodecContext->height = 1080;
    m_videoCodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    m_videoCodecContext->time_base = {1, 30};
    m_videoCodecContext->framerate = {30, 1};
    m_videoCodecContext->gop_size = 60;
    m_videoCodecContext->max_b_frames = 0;
    m_videoCodecContext->color_range = AVCOL_RANGE_MPEG;
    m_videoCodecContext->colorspace = AVCOL_SPC_BT709;
    m_videoCodecContext->color_primaries = AVCOL_PRI_BT709;
    m_videoCodecContext->color_trc = AVCOL_TRC_BT709;
    m_videoCodecContext->log_level_offset = 16;
    m_audioCodecContext->sample_rate = 48000;
    m_audioCodecContext->time_base = {1, 48000};
    m_audioCodecContext->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_audioCodecContext->bit_rate = 192000;
    av_channel_layout_default(&m_audioCodecContext->ch_layout, 2);
    m_audioCodecContext->log_level_offset = 16;
    if (m_format->oformat->flags & AVFMT_GLOBALHEADER) {
        m_videoCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        m_audioCodecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary *options = nullptr;
    av_dict_set(&options, "preset", "ultrafast", 0);
    m_videoCodecContext->bit_rate = qint64(bitrateKbps) * 1000;
    m_videoCodecContext->rc_max_rate = m_videoCodecContext->bit_rate;
    m_videoCodecContext->rc_buffer_size = bitrateKbps * 1000;
    m_videoCodecContext->thread_count = 4;
    av_dict_set(&options, "tune", "zerolatency", 0);
    av_dict_set(&options, "forced-idr", "1", 0);
    av_dict_set(&options, "x264-params",
                m_network ? "nal-hrd=cbr:force-cfr=1:scenecut=0" : "scenecut=0", 0);
    if (m_network)
        m_videoCodecContext->rc_min_rate = m_videoCodecContext->bit_rate;
    const int opened = avcodec_open2(m_videoCodecContext, videoCodec, &options);
    av_dict_free(&options);
    check(opened, "无法启动 H.264 编码器");
    check(avcodec_open2(m_audioCodecContext, audioCodec, nullptr), "无法启动 AAC 编码器");
    m_videoStream = avformat_new_stream(m_format, nullptr);
    m_audioStream = avformat_new_stream(m_format, nullptr);
    if (!m_videoStream || !m_audioStream)
        throw QStringLiteral("无法创建 MP4 音视频轨道。");
    m_videoStream->time_base = m_videoCodecContext->time_base;
    m_audioStream->time_base = m_audioCodecContext->time_base;
    check(avcodec_parameters_from_context(m_videoStream->codecpar, m_videoCodecContext),
          "无法设置视频轨道");
    check(avcodec_parameters_from_context(m_audioStream->codecpar, m_audioCodecContext),
          "无法设置音频轨道");
    m_videoFrame = av_frame_alloc();
    m_audioFrame = av_frame_alloc();
    if (!m_videoFrame || !m_audioFrame)
        throw QStringLiteral("无法分配编码帧。");
    m_videoFrame->format = m_videoCodecContext->pix_fmt;
    m_videoFrame->width = 1920;
    m_videoFrame->height = 1080;
    m_videoFrame->color_range = m_videoCodecContext->color_range;
    m_videoFrame->colorspace = m_videoCodecContext->colorspace;
    m_videoFrame->color_primaries = m_videoCodecContext->color_primaries;
    m_videoFrame->color_trc = m_videoCodecContext->color_trc;
    check(av_frame_get_buffer(m_videoFrame, 32), "无法分配视频帧缓冲");
    m_audioFrame->format = m_audioCodecContext->sample_fmt;
    m_audioFrame->sample_rate = 48000;
    m_audioFrame->nb_samples = m_audioCodecContext->frame_size;
    check(av_channel_layout_copy(&m_audioFrame->ch_layout, &m_audioCodecContext->ch_layout),
          "无法设置音频声道");
    check(av_frame_get_buffer(m_audioFrame, 0), "无法分配音频帧缓冲");
    m_pixelConversionContext =
        sws_getContext(1920, 1080, AV_PIX_FMT_RGBA, 1920, 1080, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                       nullptr, nullptr, nullptr);
    if (!m_pixelConversionContext)
        throw QStringLiteral("无法初始化视频颜色转换。");
    check(sws_setColorspaceDetails(m_pixelConversionContext, sws_getCoefficients(SWS_CS_ITU709), 1,
                                   sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16),
          "无法设置 BT.709 颜色转换");
}

void MediaEncoder::encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame)
{
    check(avcodec_send_frame(codec, frame), "无法提交编码帧");
    AVPacket *packet = av_packet_alloc();
    if (!packet)
        throw QStringLiteral("无法分配编码数据包。");
    const auto cleanup = qScopeGuard([&] {
        av_packet_free(&packet);
    });
    while (true) {
        const int result = avcodec_receive_packet(codec, packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            break;
        check(result, "编码失败");
        if (codec == m_videoCodecContext && m_expectKeyframe) {
            if (!(packet->flags & AV_PKT_FLAG_KEY))
                throw QStringLiteral("编码器未能在直播跳帧后生成关键帧。");
            m_expectKeyframe = false;
        }
        if (codec == m_videoCodecContext && packet->duration <= 0)
            packet->duration = 1;
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        const auto ioStart = MediaClock::nowNs();
        if (m_network)
            resetNetworkDeadline();
        const int written = av_interleaved_write_frame(m_format, packet);
        av_packet_unref(packet);
        if (m_network) {
            networkCheck(written, "无法发送直播数据");
            avio_flush(m_format->pb);
            networkCheck(m_format->pb->error, "直播网络写入失败");
        } else
            check(written, "无法写入录制数据");
        m_outputTimeNs += MediaClock::nowNs() - ioStart;
    }
}

void MediaEncoder::encodeVideoFrame(const QImage &image, qint64 frameIndex)
{
    const auto conversionStart = MediaClock::nowNs();
    if (!m_videoFrame || image.size() != QSize(1920, 1080) || frameIndex != m_videoFrames)
        throw QStringLiteral("视频帧尺寸或时间序号无效。");
    // FBO 通常读回为 BGRA，直接交给 swscale，避免每路输出逐帧复制并交换颜色通道。
    const bool bgra = image.format() == QImage::Format_ARGB32 ||
                      image.format() == QImage::Format_ARGB32_Premultiplied ||
                      image.format() == QImage::Format_RGB32;
    const bool rgbaNative = image.format() == QImage::Format_RGBA8888 ||
                            image.format() == QImage::Format_RGBA8888_Premultiplied;
    const auto rgba = bgra || rgbaNative ? image : image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull())
        throw QStringLiteral("无法分配录制画面缓冲。");
    check(av_frame_make_writable(m_videoFrame), "无法写入视频帧");
    m_pixelConversionContext = sws_getCachedContext(
        m_pixelConversionContext, 1920, 1080, bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA, 1920, 1080,
        AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_pixelConversionContext)
        throw QStringLiteral("无法初始化视频颜色转换。");
    check(sws_setColorspaceDetails(m_pixelConversionContext, sws_getCoefficients(SWS_CS_ITU709), 1,
                                   sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16),
          "无法设置 BT.709 颜色转换");
    const unsigned char *source[]{rgba.constBits()};
    const int stride[]{int(rgba.bytesPerLine())};
    if (sws_scale(m_pixelConversionContext, source, stride, 0, 1080, m_videoFrame->data,
                  m_videoFrame->linesize) != 1080)
        throw QStringLiteral("视频颜色转换失败。");
    m_videoFrame->pts = frameIndex;
    m_videoFrame->duration = 1;
    m_videoFrame->pict_type = m_forceKeyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    m_forceKeyframe = false;
    m_lastConversionNs = MediaClock::nowNs() - conversionStart;
    encode(m_videoCodecContext, m_videoStream, m_videoFrame);
    ++m_videoFrames;
}

void MediaEncoder::skipTo(qint64 nextFrame)
{
    if (!m_network || nextFrame < m_videoFrames)
        throw QStringLiteral("直播时间线跳转无效。");
    m_videoFrames = nextFrame;
    m_pendingAudioSamples.clear();
    m_encodedAudioFrames = nextFrame * 1600;
    m_forceKeyframe = true;
    m_expectKeyframe = true;
}

void MediaEncoder::encodeAudioSamples(const QVector<float> &samples)
{
    if (samples.size() % 2)
        throw QStringLiteral("音频帧声道数据无效。");
    m_pendingAudioSamples += samples;
    while (m_pendingAudioSamples.size() >= m_audioCodecContext->frame_size * 2)
        encodeAudio(m_audioCodecContext->frame_size);
}

void MediaEncoder::encodeAudio(int count)
{
    check(av_frame_make_writable(m_audioFrame), "无法写入音频帧");
    m_audioFrame->nb_samples = count;
    m_audioFrame->pts = m_encodedAudioFrames;
    const auto *input = m_pendingAudioSamples.constData();
    for (int channel = 0; channel < 2; ++channel) {
        auto *out = reinterpret_cast<float *>(m_audioFrame->data[channel]);
        for (int i = 0; i < count; ++i)
            out[i] = input[i * 2 + channel];
    }
    encode(m_audioCodecContext, m_audioStream, m_audioFrame);
    m_encodedAudioFrames += count;
    m_pendingAudioSamples.remove(0, count * 2);
}

void MediaEncoder::finish()
{
    if (m_completed)
        return;
    if (!m_videoFrames)
        throw QStringLiteral("未生成有效视频帧。");
    if (!m_pendingAudioSamples.isEmpty())
        encodeAudio(int(m_pendingAudioSamples.size() / 2));
    encode(m_videoCodecContext, m_videoStream, nullptr);
    encode(m_audioCodecContext, m_audioStream, nullptr);
    if (m_network) {
        resetNetworkDeadline();
        networkCheck(av_write_trailer(m_format), "无法结束直播输出");
        m_completed = true;
        return;
    }
    check(av_write_trailer(m_format), "无法完成 MP4 封装");
    avio_flush(m_io);
    check(m_io->error, "录制文件写入失败");
    if (!m_file.flush())
        throw QStringLiteral("无法刷新录制文件：%1").arg(m_file.errorString());
    m_completed = true;
}
