#include "video/camera/cameracaptureworker.h"
#include "video/camera/v4l2camera.h"
#include "util/mediaclock.h"
#include <QMutexLocker>
#include <QScopeGuard>
#include <cstring>
#include <cmath>
#include <exception>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

namespace {
QString avError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return QString::fromUtf8(text);
}

bool supportedPixel(AVPixelFormat f)
{
    switch (f) {
    case AV_PIX_FMT_NV12:
    case AV_PIX_FMT_YUYV422:
    case AV_PIX_FMT_UYVY422:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_RGB24:
    case AV_PIX_FMT_BGR24:
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR0:
    case AV_PIX_FMT_RGB0:
        return true;
    default:
        return false;
    }
}

}

// 将行填充移除，但保持像素格式和色度采样，不做 CPU 颜色转换。
std::shared_ptr<const PixelFrame> cameraPixels(const AVFrame *frame, bool &inferred)
{
    const auto f = AVPixelFormat(frame->format);
    if (!supportedPixel(f))
        return {};
    const auto *desc = av_pix_fmt_desc_get(f);
    auto pixels = std::make_shared<PixelFrame>();
    const bool rgb = desc->flags & AV_PIX_FMT_FLAG_RGB;
    const bool jpeg =
        f == AV_PIX_FMT_YUVJ420P || f == AV_PIX_FMT_YUVJ422P || f == AV_PIX_FMT_YUVJ444P;
    inferred = !rgb && (frame->color_range == AVCOL_RANGE_UNSPECIFIED ||
                        frame->colorspace == AVCOL_SPC_UNSPECIFIED);
    pixels->fullRange = rgb || jpeg || frame->color_range == AVCOL_RANGE_JPEG;
    pixels->bt709 =
        frame->colorspace == AVCOL_SPC_BT709 || (frame->colorspace == AVCOL_SPC_UNSPECIFIED &&
                                                 (frame->width >= 1280 || frame->height > 576));
    int planes = 1;
    if (f == AV_PIX_FMT_NV12) {
        pixels->layout = PixelFrame::NV12;
        planes = 2;
    } else if (f == AV_PIX_FMT_YUYV422)
        pixels->layout = PixelFrame::YUY2;
    else if (f == AV_PIX_FMT_UYVY422)
        pixels->layout = PixelFrame::UYVY;
    else if (f == AV_PIX_FMT_RGB24)
        pixels->layout = PixelFrame::RGB;
    else if (f == AV_PIX_FMT_BGR24)
        pixels->layout = PixelFrame::BGR;
    else if (f == AV_PIX_FMT_RGBA || f == AV_PIX_FMT_RGB0)
        pixels->layout = PixelFrame::RGBA;
    else if (f == AV_PIX_FMT_BGRA || f == AV_PIX_FMT_BGR0)
        pixels->layout = PixelFrame::BGRA;
    else {
        pixels->layout = PixelFrame::Planar;
        planes = 3;
    }
    for (int p = 0; p < planes; ++p) {
        int w = frame->width, h = frame->height;
        if (p) {
            w = AV_CEIL_RSHIFT(w, desc->log2_chroma_w);
            h = AV_CEIL_RSHIFT(h, desc->log2_chroma_h);
        }
        const int bytes = av_image_get_linesize(f, frame->width, p);
        if (bytes <= 0 || !frame->data[p] || std::abs(frame->linesize[p]) < bytes)
            return {};
        pixels->planeSizes[p] = QSize(w, h);
        pixels->planes[p].resize(qsizetype(bytes) * h);
        for (int y = 0; y < h; ++y)
            std::memcpy(pixels->planes[p].data() + qsizetype(y) * bytes,
                        frame->data[p] + qptrdiff(y) * frame->linesize[p], bytes);
    }
    // 摄像头没有有意义的 alpha；RGB0/BGR0 的第四字节也不能当作透明度。
    if (pixels->layout == PixelFrame::RGBA || pixels->layout == PixelFrame::BGRA)
        for (qsizetype i = 3; i < pixels->planes[0].size(); i += 4)
            pixels->planes[0][i] = char(255);
    return pixels;
}

CameraCaptureWorker::CameraCaptureWorker(CameraSource source, quint64 generation)
    : m_source(std::move(source)), m_generation(generation)
{
}

CameraCaptureWorker::~CameraCaptureWorker()
{
    stop();
    wait();
}

void CameraCaptureWorker::stop()
{
    m_stop.store(true);
}

std::shared_ptr<const CameraFrame> CameraCaptureWorker::takeLatest()
{
    QMutexLocker lock(&m_mutex);
    return std::exchange(m_latest, {});
}

QString CameraCaptureWorker::lastError() const
{
    QMutexLocker lock(&m_mutex);
    return m_error;
}

void CameraCaptureWorker::fail(const QString &error)
{
    if (m_stop.load())
        return;
    QMutexLocker lock(&m_mutex);
    m_latest.reset();
    m_error = error;
}

void CameraCaptureWorker::run()
{
    try {
        if (!m_source.hasValidCaptureSettings())
            throw QStringLiteral("摄像头配置无效。");
        V4l2Camera input(m_source);
        const auto mode = input.mode();
        const bool compressed = mode.subtype == "mjpeg" || mode.subtype == "h264";
        AVCodecContext *decoder = nullptr;
        AVPacket *packet = av_packet_alloc();
        AVFrame *decoded = av_frame_alloc();
        const auto cleanup = qScopeGuard([&] {
            avcodec_free_context(&decoder);
            av_packet_free(&packet);
            av_frame_free(&decoded);
        });
        if (!packet || !decoded) throw QStringLiteral("无法分配摄像头解码缓冲。");
        if (compressed) {
            const auto *codec = avcodec_find_decoder(mode.subtype == "mjpeg" ? AV_CODEC_ID_MJPEG : AV_CODEC_ID_H264);
            if (!codec) throw QStringLiteral("FFmpeg 缺少摄像头解码器。");
            decoder = avcodec_alloc_context3(codec);
            if (!decoder) throw QStringLiteral("无法分配摄像头解码器。");
            decoder->thread_count = 2;
            decoder->pkt_timebase = {1, 1000000000};
            if (avcodec_open2(decoder, codec, nullptr) < 0) throw QStringLiteral("无法启动摄像头解码器。");
        }
        quint64 sequence = 0;
        int damagedFrames = 0;
        auto lastPublished = MediaClock::nowNs();
        const auto publish = [&](qint64 timestamp) {
            if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > 16384 || decoded->height > 16384)
                throw QStringLiteral("摄像头帧尺寸无效。");
            if (decoded->color_trc == AVCOL_TRC_SMPTE2084 || decoded->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                (decoded->colorspace != AVCOL_SPC_UNSPECIFIED && decoded->colorspace != AVCOL_SPC_BT709 &&
                 decoded->colorspace != AVCOL_SPC_SMPTE170M && decoded->colorspace != AVCOL_SPC_BT470BG &&
                 decoded->colorspace != AVCOL_SPC_RGB))
                throw QStringLiteral("暂不支持此摄像头色彩矩阵或 HDR。");
            bool inferred = false;
            auto pixels = cameraPixels(decoded, inferred);
            if (!pixels) throw QStringLiteral("不支持的摄像头像素格式。");
            auto rendered = std::make_shared<CaptureFrame>();
            rendered->size = QSize(decoded->width, decoded->height);
            rendered->timestampNs = timestamp;
            rendered->generation = m_generation;
            rendered->sequence = ++sequence;
            rendered->pixels = std::move(pixels);
            auto frame = std::make_shared<CameraFrame>();
            frame->timestampNs = timestamp;
            frame->generation = m_generation;
            frame->sequence = sequence;
            frame->frameRateNumerator = mode.frameRateNumerator;
            frame->frameRateDenominator = mode.frameRateDenominator;
            frame->renderFrame = std::move(rendered);
            frame->status = QStringLiteral("%1 · %2 × %3 · %4 fps · %5%6")
                .arg(mode.subtype).arg(mode.width).arg(mode.height)
                .arg(double(mode.frameRateNumerator) / mode.frameRateDenominator, 0, 'g', 5)
                .arg(compressed ? QStringLiteral("CPU 软解") : QStringLiteral("未压缩"))
                .arg(inferred ? QStringLiteral(" · 色彩信息含推定") : QString());
            if (damagedFrames)
                frame->status += QStringLiteral(" · 已丢弃损坏帧 %1").arg(damagedFrames);
            QMutexLocker lock(&m_mutex);
            if (!m_stop.load()) m_latest = std::move(frame);
            lastPublished = MediaClock::nowNs();
        };
        input.start();
        while (!m_stop.load()) {
            if (MediaClock::nowNs() - lastPublished > 5000000000LL)
                throw QStringLiteral("摄像头超过五秒没有完整解码帧，请检查设备或 USB 连接。");
            qint64 timestamp = 0;
            av_frame_unref(decoded);
            av_packet_unref(packet);
            if (!input.read(packet, decoded, timestamp, m_stop)) break;
            if (!compressed) { publish(timestamp); continue; }
            int result = avcodec_send_packet(decoder, packet);
            // UVC/虚拟机传输可能产生单个损坏的 MJPEG 帧；下一张 JPEG 不依赖它。
            if (result == AVERROR_INVALIDDATA && mode.subtype == "mjpeg") {
                ++damagedFrames;
                avcodec_flush_buffers(decoder);
                continue;
            }
            if (result < 0) throw QStringLiteral("摄像头解码提交失败：%1").arg(avError(result));
            while (!m_stop.load()) {
                result = avcodec_receive_frame(decoder, decoded);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
                if (result == AVERROR_INVALIDDATA && mode.subtype == "mjpeg") {
                    ++damagedFrames;
                    avcodec_flush_buffers(decoder);
                    break;
                }
                if (result < 0) throw QStringLiteral("摄像头解码失败：%1").arg(avError(result));
                if (decoded->flags & AV_FRAME_FLAG_CORRUPT) {
                    ++damagedFrames;
                    av_frame_unref(decoded);
                    continue;
                }
                publish(decoded->pts == AV_NOPTS_VALUE ? timestamp : decoded->pts);
                av_frame_unref(decoded);
            }
        }
    } catch (const QString &message) { fail(message); }
      catch (const std::exception &error) { fail(QString::fromLocal8Bit(error.what())); }
}
