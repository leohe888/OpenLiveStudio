#include "video/camera/cameracaptureworker.h"
#include "video/camera/cameramfinput.h"
#include "graphics/gpuframeresource.h"
#include "util/mediaclock.h"
#include <QScopeGuard>
#include <QMutexLocker>
#include <cstring>
#include <cstdlib>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#ifdef Q_OS_WIN
#include <libavutil/hwcontext_d3d11va.h>
#endif
}
#ifdef Q_OS_WIN
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#endif

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

CameraCaptureWorker::CameraCaptureWorker(CameraSourceConfig config, quint64 generation)
    : m_config(std::move(config)), m_generation(generation)
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

#ifdef Q_OS_WIN
namespace {
class HardwareFrames
{
    ComPtr<ID3D11VideoDevice> video;
    ComPtr<ID3D11VideoContext> context;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    QSize size;

    struct Slot
    {
        std::shared_ptr<GpuFrameResource> resource;
        std::shared_ptr<std::atomic_bool> inUse = std::make_shared<std::atomic_bool>(false);
    };

    std::vector<Slot> pool;

public:
    bool convert(const AVFrame *frame, AVBufferRef *deviceRef, CaptureFrame &result)
    {
        auto *deviceContext = reinterpret_cast<AVHWDeviceContext *>(deviceRef->data);
        auto *hw = static_cast<AVD3D11VADeviceContext *>(deviceContext->hwctx);
        hw->lock(hw->lock_ctx);
        const auto unlock = qScopeGuard([&] {
            hw->unlock(hw->lock_ctx);
        });
        if (!processor || size != QSize(frame->width, frame->height)) {
            processor.Reset();
            enumerator.Reset();
            pool.clear();
            size = QSize(frame->width, frame->height);
            if (FAILED(hw->device->QueryInterface(IID_PPV_ARGS(&video))) ||
                FAILED(hw->device_context->QueryInterface(IID_PPV_ARGS(&context))))
                return false;
            D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc{};
            desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            desc.InputWidth = desc.OutputWidth = frame->width;
            desc.InputHeight = desc.OutputHeight = frame->height;
            desc.InputFrameRate = desc.OutputFrameRate = {30, 1};
            desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            if (FAILED(video->CreateVideoProcessorEnumerator(&desc, &enumerator)) ||
                FAILED(video->CreateVideoProcessor(enumerator.Get(), 0, &processor)))
                return false;
        }
        auto *inputTexture = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
        ComPtr<ID3D11VideoProcessorInputView> input;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC in{};
        in.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        in.Texture2D.ArraySlice = UINT(reinterpret_cast<uintptr_t>(frame->data[1]));
        if (FAILED(
                video->CreateVideoProcessorInputView(inputTexture, enumerator.Get(), &in, &input)))
            return false;
        std::shared_ptr<GpuFrameResource> output;
        for (auto &slot : pool)
            if (!slot.inUse->exchange(true)) {
                output = slot.resource;
                result.poolSlotInUse = slot.inUse;
                break;
            }
        if (!output) {
            if (pool.size() == 3)
                return true; // 所有纹理仍在消费中，丢弃本次输出。
            output = std::make_shared<GpuFrameResource>();
            output->device = hw->device;
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = frame->width;
            desc.Height = frame->height;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(hw->device->CreateTexture2D(&desc, nullptr, &output->texture)))
                return false;
            Slot slot;
            slot.resource = output;
            slot.inUse->store(true);
            result.poolSlotInUse = slot.inUse;
            pool.push_back(std::move(slot));
        }
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC out{};
        out.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorOutputView> outputView;
        if (FAILED(video->CreateVideoProcessorOutputView(output->texture.Get(), enumerator.Get(),
                                                         &out, &outputView)))
            return false;
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE color{};
        color.YCbCr_Matrix =
            frame->colorspace == AVCOL_SPC_BT709 || (frame->colorspace == AVCOL_SPC_UNSPECIFIED &&
                                                     (frame->width >= 1280 || frame->height > 576));
        color.Nominal_Range = frame->color_range == AVCOL_RANGE_JPEG ? 2 : 1;
        context->VideoProcessorSetStreamColorSpace(processor.Get(), 0, &color);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};
        rgb.RGB_Range = 0;
        rgb.Nominal_Range = 2;
        context->VideoProcessorSetOutputColorSpace(processor.Get(), &rgb);
        context->VideoProcessorSetStreamAutoProcessingMode(processor.Get(), 0, FALSE);
        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = input.Get();
        if (FAILED(context->VideoProcessorBlt(processor.Get(), outputView.Get(), 0, 1, &stream)))
            return false;
        hw->device_context->Flush();
        result.gpu = output;
        return true;
    }
};
}
#endif

void CameraCaptureWorker::run()
{
    if (!m_config.isValid()) {
        fail(QStringLiteral("摄像头配置无效。"));
        return;
    }
#ifndef Q_OS_WIN
    fail(QStringLiteral("摄像头采集目前仅支持 Windows。"));
#else
    try {
        MfCameraInput input(m_config);
        const auto *parameters = input.parameters();
        const auto &mode = input.mode();
        const bool compressed = parameters->codec_id != AV_CODEC_ID_RAWVIDEO;
        const auto *codec = compressed ? avcodec_find_decoder(parameters->codec_id) : nullptr;
        if (compressed && !codec) {
            fail(QStringLiteral("FFmpeg 缺少所需解码器。"));
            return;
        }
        AVCodecContext *decoder = nullptr;
        AVBufferRef *device = nullptr;
        AVPacket *packet = av_packet_alloc();
        AVFrame *decoded = av_frame_alloc();
        const auto cleanup = qScopeGuard([&] {
            av_frame_free(&decoded);
            av_packet_free(&packet);
            avcodec_free_context(&decoder);
            av_buffer_unref(&device);
        });
        if (!packet || !decoded) {
            fail(QStringLiteral("无法分配摄像头缓冲。"));
            return;
        }
        const QString actualFormat = mode.subtype;
        const AVRational rate{mode.frameRateNumerator, mode.frameRateDenominator};
        int ret = 0;
        bool hardware = false;
        QString fallback;
        const auto openDecoder = [&](bool tryHardware) {
            avcodec_free_context(&decoder);
            av_buffer_unref(&device);
            decoder = avcodec_alloc_context3(codec);
            if (!decoder || avcodec_parameters_to_context(decoder, parameters) < 0)
                return false;
            decoder->pkt_timebase = AVRational{1, 10000000};
            decoder->framerate = rate;
            decoder->thread_count = 2;
            if (tryHardware) {
                bool supported = false;
                for (int i = 0; const auto *config = avcodec_get_hw_config(codec, i); ++i)
                    if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
                        (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                        supported = true;
                if (!supported || av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr,
                                                         nullptr, 0) < 0)
                    return false;
                decoder->hw_device_ctx = av_buffer_ref(device);
                if (!decoder->hw_device_ctx)
                    return false;
                decoder->get_format = [](AVCodecContext *, const AVPixelFormat *formats) {
                    for (auto f = formats; *f != AV_PIX_FMT_NONE; ++f)
                        if (*f == AV_PIX_FMT_D3D11)
                            return *f;
                    return AV_PIX_FMT_NONE;
                };
            }
            return avcodec_open2(decoder, codec, nullptr) >= 0;
        };
        if (compressed && !qEnvironmentVariableIsSet("OPENLIVE_CAMERA_FORCE_SOFTWARE"))
            hardware = openDecoder(true);
        if (compressed && !hardware) {
            fallback = qEnvironmentVariableIsSet("OPENLIVE_CAMERA_FORCE_SOFTWARE")
                           ? QStringLiteral("（已指定软解）")
                           : QStringLiteral("（硬解不可用，使用软解）");
            if (!openDecoder(false)) {
                fail(QStringLiteral("无法初始化软件解码器。"));
                return;
            }
        }
        HardwareFrames hardwareFrames;
        bool waitingForKey = false;
        qint64 keyDeadline = 0;
        quint64 sequence = 0;
        const auto fallbackToSoftware = [&](const QString &reason) {
            hardware = false;
            fallback = QStringLiteral("（%1，已回退软解）").arg(reason);
            waitingForKey = parameters->codec_id == AV_CODEC_ID_H264;
            keyDeadline = MediaClock::nowNs() + 10000000000LL;
            av_frame_unref(decoded);
            return openDecoder(false);
        };
        const auto publishFrame = [&](std::shared_ptr<CaptureFrame> rendered, bool inferred,
                                      qint64 deviceTimestamp) {
            auto frame = std::make_shared<CameraFrame>();
            frame->renderFrame = rendered;
            frame->timestampNs = rendered->timestampNs;
            frame->deviceTimestamp100ns = deviceTimestamp;
            frame->sequence = rendered->sequence;
            frame->generation = m_generation;
            frame->frameRateNumerator = rate.num;
            frame->frameRateDenominator = rate.den;
            frame->status = QStringLiteral("%1 · %2 × %3 · %4%5%6")
                                .arg(actualFormat)
                                .arg(rendered->size.width())
                                .arg(rendered->size.height())
                                .arg(hardware     ? QStringLiteral("D3D11VA 硬解")
                                     : compressed ? QStringLiteral("CPU 软解")
                                                  : QStringLiteral("未压缩"))
                                .arg(fallback)
                                .arg(inferred ? QStringLiteral(" · 色彩信息含推定") : QString());
            QMutexLocker lock(&m_mutex);
            if (!m_stop.load())
                m_latest = std::move(frame);
        };
        while (!m_stop) {
            MfCameraInput::Sample sample;
            if (!input.read(packet, sample, m_stop))
                return;
            const auto releasePacket = qScopeGuard([&] {
                av_packet_unref(packet);
            });
            if (!compressed) {
                auto rendered = std::make_shared<CaptureFrame>();
                rendered->size = QSize(mode.width, mode.height);
                rendered->timestampNs = sample.arrivalNs;
                rendered->sequence = ++sequence;
                rendered->generation = m_generation;
                rendered->pixels = std::move(sample.pixels);
                publishFrame(std::move(rendered), sample.inferred, sample.timestamp100ns);
                continue;
            }
            if (hardware && m_forceSoftware &&
                !fallbackToSoftware(QStringLiteral("GPU 纹理互通失败"))) {
                fail(QStringLiteral("GPU 互通失败，且无法初始化软解。"));
                return;
            }
            if (waitingForKey) {
                if (MediaClock::nowNs() > keyDeadline) {
                    fail(QStringLiteral("软解恢复等待关键帧超时。"));
                    return;
                }
                if (!(packet->flags & AV_PKT_FLAG_KEY))
                    continue;
                waitingForKey = false;
            }
            ret = avcodec_send_packet(decoder, packet);
            if (ret < 0) {
                if (hardware && fallbackToSoftware(QStringLiteral("硬解提交失败")))
                    continue;
                fail(QStringLiteral("解码提交失败：%1").arg(avError(ret)));
                return;
            }
            while (!m_stop) {
                ret = avcodec_receive_frame(decoder, decoded);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0) {
                    if (hardware && fallbackToSoftware(QStringLiteral("硬解运行失败")))
                        break;
                    fail(QStringLiteral("视频解码失败：%1").arg(avError(ret)));
                    return;
                }
                const auto releaseFrame = qScopeGuard([&] {
                    av_frame_unref(decoded);
                });
                if (decoded->width <= 0 || decoded->height <= 0 || decoded->width > 16384 ||
                    decoded->height > 16384) {
                    fail(QStringLiteral("摄像头帧尺寸无效。"));
                    return;
                }
                if (decoded->color_trc == AVCOL_TRC_SMPTE2084 ||
                    decoded->color_trc == AVCOL_TRC_ARIB_STD_B67 ||
                    (decoded->colorspace != AVCOL_SPC_UNSPECIFIED &&
                     decoded->colorspace != AVCOL_SPC_BT709 &&
                     decoded->colorspace != AVCOL_SPC_SMPTE170M &&
                     decoded->colorspace != AVCOL_SPC_BT470BG &&
                     decoded->colorspace != AVCOL_SPC_RGB)) {
                    fail(QStringLiteral("暂不支持此 HDR 或色彩矩阵。"));
                    return;
                }
                auto rendered = std::make_shared<CaptureFrame>();
                rendered->size = QSize(decoded->width, decoded->height);
                rendered->timestampNs = sample.arrivalNs;
                rendered->sequence = ++sequence;
                rendered->generation = m_generation;
                bool inferred = false;
                if (decoded->format == AV_PIX_FMT_D3D11) {
                    const auto *frames = decoded->hw_frames_ctx
                                             ? reinterpret_cast<const AVHWFramesContext *>(
                                                   decoded->hw_frames_ctx->data)
                                             : nullptr;
                    if (!frames || !supportedPixel(frames->sw_format)) {
                        fail(QStringLiteral("暂不支持硬解后的像素格式。"));
                        return;
                    }
                    if (!hardwareFrames.convert(decoded, device, *rendered)) {
                        if (!fallbackToSoftware(QStringLiteral("GPU 颜色转换不可用"))) {
                            fail(QStringLiteral("GPU 转换失败，且无法初始化软解。"));
                            return;
                        }
                        break;
                    }
                    if (!rendered->gpu)
                        continue;
                    inferred = decoded->colorspace == AVCOL_SPC_UNSPECIFIED ||
                               decoded->color_range == AVCOL_RANGE_UNSPECIFIED;
                } else {
                    rendered->pixels = cameraPixels(decoded, inferred);
                    if (!rendered->pixels) {
                        fail(QStringLiteral("暂不支持解码后的像素格式：%1").arg(decoded->format));
                        return;
                    }
                }
                publishFrame(std::move(rendered), inferred,
                             decoded->pts == AV_NOPTS_VALUE ? sample.timestamp100ns : decoded->pts);
            }
        }
    } catch (HRESULT hr) {
        fail(cameraMfError(hr));
    } catch (const std::exception &error) {
        fail(QStringLiteral("摄像头采集失败：%1").arg(QString::fromUtf8(error.what())));
    }
#endif
}
