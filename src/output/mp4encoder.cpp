#include "mp4encoder.h"
#include <QScopeGuard>
#include <cerrno>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}
namespace {
void check(int result, const char *operation)
{
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result,message,sizeof(message));
    throw QStringLiteral("%1：%2").arg(QString::fromUtf8(operation),QString::fromUtf8(message));
}
}

Mp4Encoder::~Mp4Encoder()
{
    sws_freeContext(m_scale);
    av_frame_free(&m_videoFrame); av_frame_free(&m_audioFrame);
    avcodec_free_context(&m_video); avcodec_free_context(&m_audio);
    if (m_format) m_format->pb = nullptr;
    avformat_free_context(m_format);
    if (m_io) { av_freep(&m_io->buffer); avio_context_free(&m_io); }
    m_file.close();
    // Only remove an empty, newly-created attempt; preserve incomplete real recordings.
    if (m_created && !m_videoFrames) m_file.remove();
}
int Mp4Encoder::write(void *opaque, const unsigned char *data, int size)
{
    auto *self = static_cast<Mp4Encoder *>(opaque);
    const auto count = self->m_file.write(reinterpret_cast<const char *>(data),size);
    return count == size ? size : AVERROR(EIO);
}
qint64 Mp4Encoder::seek(void *opaque, qint64 offset, int whence)
{
    auto &file = static_cast<Mp4Encoder *>(opaque)->m_file;
    if (whence == AVSEEK_SIZE) return file.size();
    whence &= ~AVSEEK_FORCE;
    const qint64 position = whence == SEEK_SET ? offset : whence == SEEK_CUR ? file.pos()+offset
        : whence == SEEK_END ? file.size()+offset : -1;
    return position >= 0 && file.seek(position) ? file.pos() : AVERROR(EIO);
}
void Mp4Encoder::open(const QString &path, int crf)
{
    if (m_created || crf < 0 || crf > 51) throw QStringLiteral("录制编码参数无效。");
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        throw QStringLiteral("无法创建录制文件：%1").arg(m_file.errorString());
    m_created = true;
    check(avformat_alloc_output_context2(&m_format,nullptr,"mp4",nullptr),"无法创建 MP4 封装器");
    if (!m_format) throw QStringLiteral("无法分配 MP4 封装器。");
    auto *buffer = static_cast<unsigned char *>(av_malloc(65536));
    if (!buffer) throw QStringLiteral("无法分配录制写入缓冲。");
    m_io = avio_alloc_context(buffer,65536,1,this,nullptr,&Mp4Encoder::write,&Mp4Encoder::seek);
    if (!m_io) { av_free(buffer); throw QStringLiteral("无法创建录制文件输出。"); }
    m_io->seekable = AVIO_SEEKABLE_NORMAL;
    m_format->pb = m_io;
    m_format->flags |= AVFMT_FLAG_CUSTOM_IO;

    const auto *videoCodec = avcodec_find_encoder_by_name("libx264");
    const auto *audioCodec = avcodec_find_encoder_by_name("aac");
    if (!videoCodec || !audioCodec) throw QStringLiteral("FFmpeg 缺少 libx264 或 AAC 编码器。");
    m_video = avcodec_alloc_context3(videoCodec); m_audio = avcodec_alloc_context3(audioCodec);
    if (!m_video || !m_audio) throw QStringLiteral("无法分配音视频编码器。");
    m_video->width = 1920; m_video->height = 1080;
    m_video->pix_fmt = AV_PIX_FMT_YUV420P;
    m_video->time_base = {1,30}; m_video->framerate = {30,1};
    m_video->gop_size = 60; m_video->max_b_frames = 0;
    m_video->color_range = AVCOL_RANGE_MPEG;
    m_video->colorspace = AVCOL_SPC_BT709;
    m_video->color_primaries = AVCOL_PRI_BT709; m_video->color_trc = AVCOL_TRC_BT709;
    m_video->log_level_offset = 16;
    m_audio->sample_rate = 48000; m_audio->time_base = {1,48000};
    m_audio->sample_fmt = AV_SAMPLE_FMT_FLTP; m_audio->bit_rate = 192000;
    av_channel_layout_default(&m_audio->ch_layout,2);
    m_audio->log_level_offset = 16;
    if (m_format->oformat->flags & AVFMT_GLOBALHEADER) {
        m_video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER; m_audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary *options = nullptr;
    av_dict_set(&options,"preset","veryfast",0);
    av_dict_set_int(&options,"crf",crf,0);
    const int opened = avcodec_open2(m_video,videoCodec,&options);
    av_dict_free(&options); check(opened,"无法启动 H.264 编码器");
    check(avcodec_open2(m_audio,audioCodec,nullptr),"无法启动 AAC 编码器");
    m_videoStream = avformat_new_stream(m_format,nullptr);
    m_audioStream = avformat_new_stream(m_format,nullptr);
    if (!m_videoStream || !m_audioStream) throw QStringLiteral("无法创建 MP4 音视频轨道。");
    m_videoStream->time_base = m_video->time_base; m_audioStream->time_base = m_audio->time_base;
    check(avcodec_parameters_from_context(m_videoStream->codecpar,m_video),"无法设置视频轨道");
    check(avcodec_parameters_from_context(m_audioStream->codecpar,m_audio),"无法设置音频轨道");
    m_videoFrame = av_frame_alloc(); m_audioFrame = av_frame_alloc();
    if (!m_videoFrame || !m_audioFrame) throw QStringLiteral("无法分配编码帧。");
    m_videoFrame->format = m_video->pix_fmt; m_videoFrame->width = 1920; m_videoFrame->height = 1080;
    m_videoFrame->color_range = m_video->color_range; m_videoFrame->colorspace = m_video->colorspace;
    m_videoFrame->color_primaries = m_video->color_primaries; m_videoFrame->color_trc = m_video->color_trc;
    check(av_frame_get_buffer(m_videoFrame,32),"无法分配视频帧缓冲");
    m_audioFrame->format = m_audio->sample_fmt; m_audioFrame->sample_rate = 48000;
    m_audioFrame->nb_samples = m_audio->frame_size;
    check(av_channel_layout_copy(&m_audioFrame->ch_layout,&m_audio->ch_layout),"无法设置音频声道");
    check(av_frame_get_buffer(m_audioFrame,0),"无法分配音频帧缓冲");
    m_scale = sws_getContext(1920,1080,AV_PIX_FMT_RGBA,1920,1080,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
    if (!m_scale) throw QStringLiteral("无法初始化视频颜色转换。");
    check(sws_setColorspaceDetails(m_scale,sws_getCoefficients(SWS_CS_ITU709),1,
          sws_getCoefficients(SWS_CS_ITU709),0,0,1<<16,1<<16),"无法设置 BT.709 颜色转换");
    check(avformat_write_header(m_format,nullptr),"无法写入 MP4 文件头");
}
void Mp4Encoder::encode(AVCodecContext *codec, AVStream *stream, AVFrame *frame)
{
    check(avcodec_send_frame(codec,frame),"无法提交编码帧");
    AVPacket *packet = av_packet_alloc();
    if (!packet) throw QStringLiteral("无法分配编码数据包。");
    const auto cleanup = qScopeGuard([&] { av_packet_free(&packet); });
    while (true) {
        const int result = avcodec_receive_packet(codec,packet);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        check(result,"编码失败");
        if (codec==m_video && packet->duration<=0) packet->duration=1;
        av_packet_rescale_ts(packet,codec->time_base,stream->time_base);
        packet->stream_index = stream->index;
        const int written = av_interleaved_write_frame(m_format,packet);
        av_packet_unref(packet);
        check(written,"无法写入录制数据");
    }
}
void Mp4Encoder::video(const QImage &image, qint64 frameIndex)
{
    if (!m_videoFrame || image.size() != QSize(1920,1080) || frameIndex != m_videoFrames)
        throw QStringLiteral("视频帧尺寸或时间序号无效。");
    const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) throw QStringLiteral("无法分配录制画面缓冲。");
    check(av_frame_make_writable(m_videoFrame),"无法写入视频帧");
    const unsigned char *source[]{rgba.constBits()}; const int stride[]{int(rgba.bytesPerLine())};
    if (sws_scale(m_scale,source,stride,0,1080,m_videoFrame->data,m_videoFrame->linesize) != 1080)
        throw QStringLiteral("视频颜色转换失败。");
    m_videoFrame->pts = frameIndex;
    m_videoFrame->duration = 1;
    encode(m_video,m_videoStream,m_videoFrame); ++m_videoFrames;
}
void Mp4Encoder::audio(const QVector<float> &samples)
{
    if (samples.size() % 2) throw QStringLiteral("音频帧声道数据无效。");
    m_pending += samples;
    while (m_pending.size() >= m_audio->frame_size * 2) encodeAudio(m_audio->frame_size);
}
void Mp4Encoder::encodeAudio(int count)
{
    check(av_frame_make_writable(m_audioFrame),"无法写入音频帧");
    m_audioFrame->nb_samples = count; m_audioFrame->pts = m_audioSamples;
    for (int channel = 0; channel < 2; ++channel) {
        auto *out = reinterpret_cast<float *>(m_audioFrame->data[channel]);
        for (int i = 0; i < count; ++i) out[i] = m_pending[i*2+channel];
    }
    encode(m_audio,m_audioStream,m_audioFrame);
    m_audioSamples += count; m_pending.remove(0,count*2);
}
void Mp4Encoder::finish()
{
    if (m_completed) return;
    if (!m_videoFrames) throw QStringLiteral("未生成有效视频帧。");
    if (!m_pending.isEmpty()) encodeAudio(int(m_pending.size()/2));
    encode(m_video,m_videoStream,nullptr); encode(m_audio,m_audioStream,nullptr);
    check(av_write_trailer(m_format),"无法完成 MP4 封装");
    avio_flush(m_io);
    check(m_io->error,"录制文件写入失败");
    if (!m_file.flush()) throw QStringLiteral("无法刷新录制文件：%1").arg(m_file.errorString());
    m_completed = true;
}
