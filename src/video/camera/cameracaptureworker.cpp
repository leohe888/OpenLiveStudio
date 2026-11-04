#include "video/camera/cameracaptureworker.h"
#include "graphics/gpuframeresource.h"
#include "util/mediaclock.h"
#include <QScopeGuard>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>
#include <numeric>
#include <cstring>
extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
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
#include <dshow.h>
#include <dvdmedia.h>
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

QString inputFormat(AVCodecID codec, AVPixelFormat pixel)
{
    if (codec == AV_CODEC_ID_RAWVIDEO) {
        const char *name = av_get_pix_fmt_name(pixel);
        return name ? QString::fromLatin1(name) : QString();
    }
    return QString::fromLatin1(avcodec_get_name(codec));
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

bool supportedInput(const QString &format)
{
    return format == "mjpeg" || format == "h264" ||
           supportedPixel(av_get_pix_fmt(format.toLatin1().constData()));
}

#ifdef Q_OS_WIN
struct ComScope
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ~ComScope()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

struct DeviceMoniker
{
    CameraDevice device;
    ComPtr<IMoniker> moniker;
};

QList<DeviceMoniker> monikers(QString *error)
{
    ComPtr<ICreateDevEnum> enumerator;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("无法枚举摄像头：0x%1").arg(quint32(hr), 8, 16, QChar('0'));
        return {};
    }
    ComPtr<IEnumMoniker> entries;
    hr = enumerator->CreateClassEnumerator(CLSID_VideoInputDeviceCategory, &entries, 0);
    if (hr == S_FALSE)
        return {};
    if (FAILED(hr)) {
        if (error)
            *error = QStringLiteral("无法读取摄像头列表。");
        return {};
    }
    QList<DeviceMoniker> result;
    ComPtr<IMoniker> moniker;
    while (entries->Next(1, &moniker, nullptr) == S_OK) {
        ComPtr<IPropertyBag> bag;
        LPOLESTR display = nullptr;
        if (SUCCEEDED(moniker->BindToStorage(nullptr, nullptr, IID_PPV_ARGS(&bag))) &&
            SUCCEEDED(moniker->GetDisplayName(nullptr, nullptr, &display))) {
            VARIANT name;
            VariantInit(&name);
            if (SUCCEEDED(bag->Read(L"FriendlyName", &name, nullptr)) && name.vt == VT_BSTR) {
                // FFmpeg dshow 使用 moniker display name，并将冒号替换成下划线。
                result.append({{QString::fromWCharArray(display).replace(':', '_'),
                                QString::fromWCharArray(name.bstrVal)},
                               moniker});
            }
            VariantClear(&name);
            CoTaskMemFree(display);
        }
        moniker.Reset();
    }
    return result;
}

QString subtypeName(const GUID &subtype, int bits)
{
    if (subtype == MEDIASUBTYPE_RGB24)
        return "bgr24";
    if (subtype == MEDIASUBTYPE_RGB32 || subtype == MEDIASUBTYPE_ARGB32)
        return "bgra";
    const DWORD code = subtype.Data1;
    const auto fourcc = [](char a, char b, char c, char d) {
        return DWORD(BYTE(a)) | DWORD(BYTE(b)) << 8 | DWORD(BYTE(c)) << 16 | DWORD(BYTE(d)) << 24;
    };
    if (code == fourcc('M', 'J', 'P', 'G'))
        return "mjpeg";
    if (code == fourcc('H', '2', '6', '4') || code == fourcc('h', '2', '6', '4'))
        return "h264";
    if (code == fourcc('N', 'V', '1', '2'))
        return "nv12";
    if (code == fourcc('Y', 'U', 'Y', '2'))
        return "yuyv422";
    if (code == fourcc('U', 'Y', 'V', 'Y'))
        return "uyvy422";
    if (code == fourcc('I', '4', '2', '0') || code == fourcc('I', 'Y', 'U', 'V') ||
        code == fourcc('Y', 'V', '1', '2'))
        return "yuv420p";
    // 原始数据布局由 FFmpeg 的 rawvideo 解码器解释（包括 YV12 平面顺序）。
    Q_UNUSED(bits);
    OLECHAR text[40]{};
    StringFromGUID2(subtype, text, 40);
    return QString::fromWCharArray(text);
}
#endif
}

QPair<int, int> cameraFrameRateFromInterval(qint64 interval)
{
    if (interval <= 0)
        return {0, 1};
    // DirectShow 的 100ns 精度将 30 FPS 量化成 333333；请求时需还原标准分数。
    for (const AVRational rate : {AVRational{1, 1},
                                  {5, 1},
                                  {15, 2},
                                  {10, 1},
                                  {15, 1},
                                  {24, 1},
                                  {25, 1},
                                  {30, 1},
                                  {50, 1},
                                  {60, 1},
                                  {120, 1},
                                  {240, 1},
                                  {24000, 1001},
                                  {30000, 1001},
                                  {60000, 1001},
                                  {120000, 1001}}) {
        if (std::abs(av_rescale(10000000, rate.den, rate.num) - interval) <= 1)
            return {rate.num, rate.den};
    }
    int numerator = 0, denominator = 1;
    av_reduce(&numerator, &denominator, 10000000, interval, INT_MAX);
    return {numerator, denominator};
}

QList<CameraDevice> cameraDevices(QString *error)
{
    if (error)
        error->clear();
#ifdef Q_OS_WIN
    ComScope com;
    QList<CameraDevice> result;
    for (const auto &entry : monikers(error))
        result.append(entry.device);
    return result;
#else
    if (error)
        *error = QStringLiteral("摄像头采集目前仅支持 Windows。");
    return {};
#endif
}

QList<CameraMode> cameraModes(const QString &deviceId, QString *error)
{
    if (error)
        error->clear();
    QList<CameraMode> result;
#ifdef Q_OS_WIN
    ComScope com;
    for (const auto &entry : monikers(error)) {
        if (entry.device.id != deviceId)
            continue;
        ComPtr<IBaseFilter> filter;
        if (FAILED(entry.moniker->BindToObject(nullptr, nullptr, IID_PPV_ARGS(&filter))))
            break;
        ComPtr<IEnumPins> pins;
        if (FAILED(filter->EnumPins(&pins)))
            break;
        ComPtr<IPin> pin;
        while (pins->Next(1, &pin, nullptr) == S_OK) {
            const auto releasePin = qScopeGuard([&] {
                pin.Reset();
            });
            PIN_DIRECTION direction;
            if (FAILED(pin->QueryDirection(&direction)) || direction != PINDIR_OUTPUT)
                continue;
            ComPtr<IKsPropertySet> properties;
            GUID category{};
            DWORD returned = 0;
            if (SUCCEEDED(pin.As(&properties)) &&
                SUCCEEDED(properties->Get(AMPROPSETID_Pin, AMPROPERTY_PIN_CATEGORY, nullptr, 0,
                                          &category, sizeof(category), &returned)) &&
                category != PIN_CATEGORY_CAPTURE)
                continue;
            ComPtr<IAMStreamConfig> config;
            if (FAILED(pin.As(&config)))
                continue;
            int count = 0, bytes = 0;
            if (FAILED(config->GetNumberOfCapabilities(&count, &bytes)) ||
                bytes < int(sizeof(VIDEO_STREAM_CONFIG_CAPS)))
                continue;
            QByteArray caps(bytes, 0);
            for (int i = 0; i < count; ++i) {
                AM_MEDIA_TYPE *type = nullptr;
                if (FAILED(config->GetStreamCaps(i, &type, reinterpret_cast<BYTE *>(caps.data()))))
                    continue;
                const auto freeType = qScopeGuard([&] {
                    CoTaskMemFree(type->pbFormat);
                    if (type->pUnk)
                        type->pUnk->Release();
                    CoTaskMemFree(type);
                });
                BITMAPINFOHEADER bitmap{};
                REFERENCE_TIME interval = 0;
                if (type->formattype == FORMAT_VideoInfo &&
                    type->cbFormat >= sizeof(VIDEOINFOHEADER)) {
                    const auto *v = reinterpret_cast<VIDEOINFOHEADER *>(type->pbFormat);
                    bitmap = v->bmiHeader;
                    interval = v->AvgTimePerFrame;
                } else if (type->formattype == FORMAT_VideoInfo2 &&
                           type->cbFormat >= sizeof(VIDEOINFOHEADER2)) {
                    const auto *v = reinterpret_cast<VIDEOINFOHEADER2 *>(type->pbFormat);
                    bitmap = v->bmiHeader;
                    interval = v->AvgTimePerFrame;
                } else
                    continue;
                const auto *range =
                    reinterpret_cast<const VIDEO_STREAM_CONFIG_CAPS *>(caps.constData());
                QList<qint64> intervals{interval, range->MinFrameInterval, range->MaxFrameInterval};
                for (int fps : {15, 24, 25, 30, 50, 60, 120}) {
                    qint64 t = 10000000LL / fps;
                    if (t >= range->MinFrameInterval && t <= range->MaxFrameInterval)
                        intervals.append(t);
                }
                for (auto t : intervals) {
                    if (t <= 0 || bitmap.biWidth <= 0 || bitmap.biHeight == 0)
                        continue;
                    // 部分驱动的默认帧间隔不属于该格式范围，不能将它作为可用模式。
                    if ((range->MinFrameInterval > 0 && t < range->MinFrameInterval) ||
                        (range->MaxFrameInterval > 0 && t > range->MaxFrameInterval))
                        continue;
                    const auto fps = cameraFrameRateFromInterval(t);
                    CameraMode mode{bitmap.biWidth, std::abs(bitmap.biHeight), fps.first,
                                    fps.second, subtypeName(type->subtype, bitmap.biBitCount)};
                    mode.supported = supportedInput(mode.subtype);
                    mode.name =
                        QStringLiteral("%1 × %2 · %3 FPS")
                            .arg(mode.width)
                            .arg(mode.height)
                            .arg(double(mode.frameRateNumerator) / mode.frameRateDenominator, 0,
                                 'g', 6);
                    if (!std::any_of(result.cbegin(), result.cend(), [&](const auto &m) {
                            return m.sameFormat(mode);
                        }))
                        result.append(mode);
                }
            }
        }
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            if (a.width != b.width)
                return a.width < b.width;
            if (a.height != b.height)
                return a.height < b.height;
            return double(a.frameRateNumerator) / a.frameRateDenominator <
                   double(b.frameRateNumerator) / b.frameRateDenominator;
        });
        if (result.isEmpty() && error)
            *error = QStringLiteral("无法读取摄像头模式。");
        return result;
    }
#endif
    if (error && error->isEmpty())
        *error = QStringLiteral("摄像头不可用或已断开。");
    return result;
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
    ComScope com;
    AVFormatContext *input = avformat_alloc_context();
    AVCodecContext *decoder = nullptr;
    AVBufferRef *device = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *decoded = av_frame_alloc();
    const auto cleanup = qScopeGuard([&] {
        av_frame_free(&decoded);
        av_packet_free(&packet);
        avcodec_free_context(&decoder);
        av_buffer_unref(&device);
        avformat_close_input(&input);
    });
    if (!input || !packet || !decoded) {
        fail(QStringLiteral("无法分配摄像头缓冲。"));
        return;
    }

    struct Interrupt
    {
        std::atomic_bool *stop;
        qint64 deadline;
    } interrupt{&m_stop, MediaClock::nowNs() + 10000000000LL};

    input->interrupt_callback = {[](void *opaque) {
                                     const auto &i = *static_cast<Interrupt *>(opaque);
                                     return int(i.stop->load() || MediaClock::nowNs() > i.deadline);
                                 },
                                 &interrupt};
    avdevice_register_all();
    AVDictionary *options = nullptr;
    const auto freeOptions = qScopeGuard([&] {
        av_dict_free(&options);
    });
    const auto setOption = [&](const char *key, const QString &value) {
        av_dict_set(&options, key, value.toUtf8().constData(), 0);
    };
    if (!m_config.resolution().isEmpty()) {
        setOption("video_size", QStringLiteral("%1x%2")
                                    .arg(m_config.resolution().width())
                                    .arg(m_config.resolution().height()));
        setOption("framerate", QStringLiteral("%1/%2")
                                   .arg(m_config.frameRateNumerator())
                                   .arg(m_config.frameRateDenominator()));
    }
    if (!m_config.pixelFormat().isEmpty()) {
        if (!supportedInput(m_config.pixelFormat())) {
            fail(QStringLiteral("不支持所选采集格式。"));
            return;
        }
        if (m_config.pixelFormat() == "mjpeg" || m_config.pixelFormat() == "h264")
            input->video_codec_id =
                avcodec_descriptor_get_by_name(m_config.pixelFormat().toLatin1().constData())->id;
        else
            setOption("pixel_format", m_config.pixelFormat());
    }
    setOption("rtbufsize", "16777216");
    const auto url = (QStringLiteral("video=") + m_config.deviceId()).toUtf8();
    int ret = avformat_open_input(&input, url.constData(), av_find_input_format("dshow"), &options);
    if (ret < 0) {
        if (!m_stop)
            fail(QStringLiteral("无法打开摄像头或所选模式：%1").arg(avError(ret)));
        return;
    }
    int streamIndex = -1;
    for (unsigned i = 0; i < input->nb_streams; ++i)
        if (input->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            streamIndex = int(i);
            break;
        }
    if (streamIndex < 0) {
        fail(QStringLiteral("摄像头没有视频流。"));
        return;
    }
    AVStream *stream = input->streams[streamIndex];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        fail(QStringLiteral("FFmpeg 缺少所需解码器。"));
        return;
    }
    const QString actualFormat =
        inputFormat(stream->codecpar->codec_id, AVPixelFormat(stream->codecpar->format));
    if (!supportedInput(actualFormat) ||
        (!m_config.pixelFormat().isEmpty() && m_config.pixelFormat() != actualFormat)) {
        fail(QStringLiteral("设备未采用支持的采集格式：%1").arg(actualFormat));
        return;
    }
    const AVRational rate =
        stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;
    if (!m_config.resolution().isEmpty() &&
        (m_config.resolution() != QSize(stream->codecpar->width, stream->codecpar->height) ||
         (rate.num && std::abs(av_q2d(rate) - double(m_config.frameRateNumerator()) /
                                                  m_config.frameRateDenominator()) > .01))) {
        fail(QStringLiteral("设备未采用所选分辨率或帧率。"));
        return;
    }
    bool hardware = false;
    QString fallback;
    const auto openDecoder = [&](bool tryHardware) {
        avcodec_free_context(&decoder);
        av_buffer_unref(&device);
        decoder = avcodec_alloc_context3(codec);
        if (!decoder || avcodec_parameters_to_context(decoder, stream->codecpar) < 0)
            return false;
        decoder->pkt_timebase = stream->time_base;
        decoder->thread_count = 2;
        if (tryHardware) {
            bool supported = false;
            for (int i = 0; const auto *config = avcodec_get_hw_config(codec, i); ++i)
                if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
                    (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
                    supported = true;
            if (!supported ||
                av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0) < 0)
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
    const bool compressed = stream->codecpar->codec_id != AV_CODEC_ID_RAWVIDEO;
    if (compressed && !qEnvironmentVariableIsSet("OPENLIVE_CAMERA_FORCE_SOFTWARE"))
        hardware = openDecoder(true);
    if (!hardware) {
        if (compressed)
            fallback = QStringLiteral("（硬解不可用，使用软解）");
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
        waitingForKey = stream->codecpar->codec_id == AV_CODEC_ID_H264;
        keyDeadline = MediaClock::nowNs() + 10000000000LL;
        av_frame_unref(decoded);
        return openDecoder(false);
    };
    while (!m_stop) {
        interrupt.deadline = MediaClock::nowNs() + 5000000000LL;
        ret = av_read_frame(input, packet);
        if (ret == AVERROR(EAGAIN)) {
            QThread::msleep(2);
            continue;
        }
        if (ret < 0) {
            if (!m_stop)
                fail(QStringLiteral("摄像头断开或采集失败：%1").arg(avError(ret)));
            return;
        }
        const auto releasePacket = qScopeGuard([&] {
            av_packet_unref(packet);
        });
        if (packet->stream_index != streamIndex)
            continue;
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
            rendered->timestampNs = MediaClock::nowNs();
            rendered->sequence = ++sequence;
            rendered->generation = m_generation;
            bool inferred = false;
            if (decoded->format == AV_PIX_FMT_D3D11) {
                const auto *frames =
                    decoded->hw_frames_ctx
                        ? reinterpret_cast<const AVHWFramesContext *>(decoded->hw_frames_ctx->data)
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
            auto frame = std::make_shared<CameraFrame>();
            frame->renderFrame = rendered;
            frame->timestampNs = rendered->timestampNs;
            frame->sequence = sequence;
            frame->generation = m_generation;
            frame->frameRateNumerator = rate.num;
            frame->frameRateDenominator = rate.den ? rate.den : 1;
            frame->status = QStringLiteral("%1 · %2 × %3 · %4%5%6")
                                .arg(actualFormat)
                                .arg(decoded->width)
                                .arg(decoded->height)
                                .arg(hardware     ? QStringLiteral("D3D11VA 硬解")
                                     : compressed ? QStringLiteral("CPU 软解")
                                                  : QStringLiteral("未压缩"))
                                .arg(fallback)
                                .arg(inferred ? QStringLiteral(" · 色彩信息含推定") : QString());
            QMutexLocker lock(&m_mutex);
            m_latest = std::move(frame); // 只保留最新的完整解码帧，不丢弃有依赖关系的压缩包。
        }
    }
#endif
}
