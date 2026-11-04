#include "video/camera/cameracaptureworker.h"
#include "graphics/gpuframeresource.h"
#include "util/mediaclock.h"
#include <QScopeGuard>
#include <QMutexLocker>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
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
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
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

#ifdef Q_OS_WIN
namespace {
void check(HRESULT hr)
{
    if (FAILED(hr))
        throw hr;
}

struct MfRuntime
{
    MfRuntime()
    {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            CoUninitialize();
            throw hr;
        }
    }

    ~MfRuntime()
    {
        MFShutdown();
        CoUninitialize();
    }
};

QString stringAttribute(IMFAttributes *attributes, REFGUID key)
{
    WCHAR *text = nullptr;
    check(attributes->GetAllocatedString(key, &text, nullptr));
    const auto release = qScopeGuard([&] {
        CoTaskMemFree(text);
    });
    return QString::fromWCharArray(text);
}

ComPtr<IMFMediaSource> openCamera(const QString &id)
{
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(&attributes, 2));
    check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                              MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    check(attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                reinterpret_cast<LPCWSTR>(id.utf16())));
    ComPtr<IMFMediaSource> source;
    check(MFCreateDeviceSource(attributes.Get(), &source));
    return source;
}

QString formatName(REFGUID subtype)
{
    if (subtype == MFVideoFormat_RGB24)
        return "bgr24";
    if (subtype == MFVideoFormat_RGB32)
        return "bgr0";
    if (subtype == MFVideoFormat_ARGB32)
        return "bgra";
    if (subtype == MFVideoFormat_NV12)
        return "nv12";
    if (subtype == MFVideoFormat_YUY2)
        return "yuyv422";
    if (subtype == MFVideoFormat_UYVY)
        return "uyvy422";
    if (subtype == MFVideoFormat_I420 || subtype == MFVideoFormat_IYUV ||
        subtype == MFVideoFormat_YV12)
        return "yuv420p";
    if (subtype == MFVideoFormat_MJPG)
        return "mjpeg";
    if (subtype == MFVideoFormat_H264)
        return "h264";
    WCHAR text[40]{};
    StringFromGUID2(subtype, text, 40);
    return QString::fromWCharArray(text);
}

CameraMode readMode(IMFMediaType *type)
{
    UINT32 w = 0, h = 0, n = 0, d = 0;
    GUID subtype{};
    if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &w, &h)) ||
        FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &n, &d)) ||
        FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || !w || !h || w > 16384 || h > 16384 ||
        !n || !d || n > INT_MAX || d > INT_MAX)
        return {};
    CameraMode mode{int(w), int(h), int(n), int(d), formatName(subtype)};
    mode.supported = !mode.subtype.startsWith('{');
    mode.name = QStringLiteral("%1 × %2 · %3 FPS").arg(w).arg(h).arg(double(n) / d, 0, 'g', 6);
    return mode;
}

bool matches(const CameraSourceConfig &config, const CameraMode &mode)
{
    return mode.isValid() && mode.supported &&
           (config.pixelFormat().isEmpty() || config.pixelFormat() == mode.subtype) &&
           (config.resolution().isEmpty() ||
            (config.resolution() == QSize(mode.width, mode.height) &&
             qint64(config.frameRateNumerator()) * mode.frameRateDenominator ==
                 qint64(mode.frameRateNumerator) * config.frameRateDenominator()));
}

// 使用媒体源的原生类型处理器，不为模式查询启动解码或颜色转换。
ComPtr<IMFMediaTypeHandler> videoTypes(IMFMediaSource *source)
{
    ComPtr<IMFPresentationDescriptor> presentation;
    check(source->CreatePresentationDescriptor(&presentation));
    DWORD count = 0;
    check(presentation->GetStreamDescriptorCount(&count));
    for (DWORD i = 0; i < count; ++i) {
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> stream;
        ComPtr<IMFMediaTypeHandler> handler;
        check(presentation->GetStreamDescriptorByIndex(i, &selected, &stream));
        check(stream->GetMediaTypeHandler(&handler));
        GUID major{};
        check(handler->GetMajorType(&major));
        if (major == MFMediaType_Video)
            return handler;
    }
    throw HRESULT(MF_E_INVALIDMEDIATYPE);
}

void setColor(AVFrame &frame, IMFMediaType *type)
{
    frame.color_range = AVCOL_RANGE_UNSPECIFIED;
    frame.colorspace = AVCOL_SPC_UNSPECIFIED;
    frame.color_trc = AVCOL_TRC_UNSPECIFIED;
    switch (MFGetAttributeUINT32(type, MF_MT_YUV_MATRIX, MFVideoTransferMatrix_Unknown)) {
    case MFVideoTransferMatrix_BT709:
        frame.colorspace = AVCOL_SPC_BT709;
        break;
    case MFVideoTransferMatrix_BT601:
        frame.colorspace = AVCOL_SPC_SMPTE170M;
        break;
    case MFVideoTransferMatrix_Unknown:
        break;
    default:
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    }
    switch (MFGetAttributeUINT32(type, MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_Unknown)) {
    case MFNominalRange_0_255:
        frame.color_range = AVCOL_RANGE_JPEG;
        break;
    case MFNominalRange_16_235:
        frame.color_range = AVCOL_RANGE_MPEG;
        break;
    case MFNominalRange_Unknown:
        break;
    default:
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    }
    const auto transfer =
        MFGetAttributeUINT32(type, MF_MT_TRANSFER_FUNCTION, MFVideoTransFunc_Unknown);
    if (transfer == MFVideoTransFunc_2084 || transfer == MFVideoTransFunc_HLG)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
}

struct ReaderState
{
    std::mutex mutex;
    std::condition_variable ready;
    ComPtr<IMFSample> sample;
    HRESULT error = S_OK;
    LONGLONG timestamp = 0;
    qint64 arrival = 0;
    int active = 0;
    bool completed = false, stopped = false, flushed = false;
};

class ReaderCallback final : public IMFSourceReaderCallback
{
    std::atomic<ULONG> references{1};
    std::shared_ptr<ReaderState> state;

public:
    explicit ReaderCallback(std::shared_ptr<ReaderState> s) : state(std::move(s))
    {
    }

    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IMFSourceReaderCallback))
            return E_NOINTERFACE;
        *out = static_cast<IMFSourceReaderCallback *>(this);
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++references;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG remaining = --references;
        if (!remaining)
            delete this;
        return remaining;
    }

    STDMETHODIMP OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG time,
                              IMFSample *sample) override
    {
        std::lock_guard lock(state->mutex);
        ++state->active;
        if (!state->stopped && SUCCEEDED(state->error)) {
            state->error = FAILED(hr) ? hr
                           : flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED |
                                      MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)
                               ? MF_E_INVALIDMEDIATYPE
                           : flags & MF_SOURCE_READERF_ENDOFSTREAM ? MF_E_END_OF_STREAM
                                                                   : S_OK;
            state->sample = sample;
            state->timestamp = time;
            state->arrival = MediaClock::nowNs();
            state->completed = true;
        }
        --state->active;
        state->ready.notify_one();
        return S_OK;
    }

    STDMETHODIMP OnFlush(DWORD) override
    {
        std::lock_guard lock(state->mutex);
        state->flushed = true;
        state->ready.notify_one();
        return S_OK;
    }

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *event) override
    {
        HRESULT hr = S_OK;
        if (event && SUCCEEDED(event->GetStatus(&hr)) && FAILED(hr)) {
            std::lock_guard lock(state->mutex);
            state->error = hr;
            state->completed = true;
            state->ready.notify_one();
        }
        return S_OK;
    }
};
}

QString cameraMfError(HRESULT hr)
{
    return QObject::tr("摄像头操作失败（0x%1），请检查所选格式、设备连接、占用情况和相机访问权限。")
        .arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

// 锁定范围内只整理行和 UV 顺序，不转换像素颜色。
std::shared_ptr<const PixelFrame> cameraMfPixels(IMFSample *sample, IMFMediaType *type,
                                                 bool &inferred)
{
    const auto mode = readMode(type);
    if (!mode.isValid() || !mode.supported)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    GUID subtype{};
    check(type->GetGUID(MF_MT_SUBTYPE, &subtype));
    const auto format = av_get_pix_fmt(mode.subtype.toLatin1().constData());
    if (format == AV_PIX_FMT_NONE)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer));
    ComPtr<IMF2DBuffer2> plane;
    BYTE *data = nullptr, *begin = nullptr;
    DWORD length = 0;
    LONG stride = 0;
    const bool twoD = SUCCEEDED(buffer.As(&plane));
    if (twoD)
        check(plane->Lock2DSize(MF2DBuffer_LockFlags_Read, &data, &stride, &begin, &length));
    else {
        UINT32 encoded = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &encoded)))
            stride = LONG(encoded);
        else
            check(MFGetStrideForBitmapInfoHeader(subtype.Data1, mode.width, &stride));
        check(buffer->Lock(&begin, nullptr, &length));
        data = begin;
        if (stride < 0) {
            const quint64 offset = quint64(-qint64(stride)) * (mode.height - 1);
            if (offset >= length) {
                buffer->Unlock();
                throw HRESULT(MF_E_BUFFERTOOSMALL);
            }
            data += offset;
        }
    }
    const auto unlock = qScopeGuard([&] {
        if (twoD)
            plane->Unlock2D();
        else
            buffer->Unlock();
    });
    AVFrame frame{};
    frame.width = mode.width;
    frame.height = mode.height;
    frame.format = format;
    setColor(frame, type);
    frame.data[0] = data;
    frame.linesize[0] = stride;
    if (format == AV_PIX_FMT_NV12 || format == AV_PIX_FMT_YUV420P) {
        if (stride <= 0 || mode.width % 2 || mode.height % 2 ||
            (format == AV_PIX_FMT_YUV420P && stride % 2))
            throw HRESULT(MF_E_INVALIDMEDIATYPE);
        const quint64 yBytes = quint64(stride) * mode.height;
        const quint64 chromaStride = format == AV_PIX_FMT_NV12 ? stride : stride / 2;
        const quint64 chromaBytes = chromaStride * (mode.height / 2);
        const quint64 total = yBytes + chromaBytes * (format == AV_PIX_FMT_NV12 ? 1 : 2);
        if (quintptr(data) < quintptr(begin) || quintptr(data) - quintptr(begin) + total > length)
            throw HRESULT(MF_E_BUFFERTOOSMALL);
        frame.data[1] = data + yBytes;
        frame.linesize[1] = int(chromaStride);
        if (format == AV_PIX_FMT_YUV420P) {
            frame.data[2] = frame.data[1] + chromaBytes;
            frame.linesize[2] = frame.linesize[1];
            if (subtype == MFVideoFormat_YV12)
                std::swap(frame.data[1], frame.data[2]);
        }
    }
    const auto *desc = av_pix_fmt_desc_get(format);
    const int planes = av_pix_fmt_count_planes(format);
    for (int p = 0; p < planes; ++p) {
        const int rows = p ? AV_CEIL_RSHIFT(mode.height, desc->log2_chroma_h) : mode.height;
        const int rowBytes = av_image_get_linesize(format, mode.width, p);
        const qint64 first = qint64(quintptr(frame.data[p])) - qint64(quintptr(begin));
        const qint64 last = first + qint64(rows - 1) * frame.linesize[p];
        if (rowBytes <= 0 || std::abs(qint64(frame.linesize[p])) < rowBytes ||
            std::min(first, last) < 0 || std::max(first, last) + rowBytes > length)
            throw HRESULT(MF_E_BUFFERTOOSMALL);
    }
    auto pixels = cameraPixels(&frame, inferred);
    if (!pixels)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    return pixels;
}

void cameraMfParameters(IMFMediaType *type, AVCodecParameters *par)
{
    const auto mode = readMode(type);
    if (!mode.isValid() || !mode.supported)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = mode.subtype == "mjpeg"  ? AV_CODEC_ID_MJPEG
                    : mode.subtype == "h264" ? AV_CODEC_ID_H264
                                             : AV_CODEC_ID_RAWVIDEO;
    par->width = mode.width;
    par->height = mode.height;
    par->format = av_get_pix_fmt(mode.subtype.toLatin1().constData());
    AVFrame colors{};
    setColor(colors, type);
    par->color_space = colors.colorspace;
    par->color_range = colors.color_range;
    if (par->codec_id == AV_CODEC_ID_H264) {
        UINT32 bytes = 0;
        if (SUCCEEDED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &bytes)) && bytes) {
            if (bytes > 1024 * 1024)
                throw HRESULT(MF_E_INVALIDMEDIATYPE);
            par->extradata =
                static_cast<uint8_t *>(av_mallocz(bytes + AV_INPUT_BUFFER_PADDING_SIZE));
            if (!par->extradata)
                throw HRESULT(E_OUTOFMEMORY);
            check(type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, par->extradata, bytes, nullptr));
            par->extradata_size = int(bytes);
        }
    }
}

void cameraMfPacket(IMFSample *sample, AVPacket *packet, qint64 timestamp100ns, bool intraFrame)
{
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer));
    BYTE *bytes = nullptr;
    DWORD length = 0;
    check(buffer->Lock(&bytes, nullptr, &length));
    const auto unlock = qScopeGuard([&] {
        buffer->Unlock();
    });
    if (!length || length > 64 * 1024 * 1024)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    if (av_new_packet(packet, int(length)) < 0)
        throw HRESULT(E_OUTOFMEMORY);
    memcpy(packet->data, bytes, length);
    packet->pts = timestamp100ns;
    UINT64 decodeTime = 0;
    packet->dts = SUCCEEDED(sample->GetUINT64(MFSampleExtension_DecodeTimestamp, &decodeTime))
                      ? LONGLONG(decodeTime)
                      : AV_NOPTS_VALUE;
    LONGLONG duration = 0;
    if (SUCCEEDED(sample->GetSampleDuration(&duration)))
        packet->duration = duration;
    bool key = intraFrame || MFGetAttributeUINT32(sample, MFSampleExtension_CleanPoint, FALSE);
    // 部分摄像头缺少 CleanPoint，H.264 原生类型使用 Annex B，检查 IDR NAL。
    for (DWORD i = 0; !key && i + 3 < length; ++i)
        if (!bytes[i] && !bytes[i + 1] && bytes[i + 2] == 1 && (bytes[i + 3] & 31) == 5)
            key = true;
    if (key)
        packet->flags |= AV_PKT_FLAG_KEY;
}

namespace {
// MF 只负责设备原生格式，不插入解码器或 RGB 转换器。
class MfCameraInput
{
public:
    explicit MfCameraInput(const CameraSourceConfig &config);
    ~MfCameraInput();
    const CameraMode &mode() const;
    const AVCodecParameters *parameters() const;

    struct Sample
    {
        std::shared_ptr<const PixelFrame> pixels;
        qint64 timestamp100ns = 0, arrivalNs = 0;
        bool inferred = false;
    };

    // 压缩样本填入 packet；未压缩样本保留原始像素布局。
    bool read(AVPacket *packet, Sample &sample, const std::atomic_bool &stop);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

struct MfCameraInput::Impl
{
    MfRuntime runtime;
    ComPtr<IMFMediaSource> source;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFMediaType> type;
    std::shared_ptr<ReaderState> state = std::make_shared<ReaderState>();
    AVCodecParameters *parameters = avcodec_parameters_alloc();
    CameraMode mode;
    bool readStarted = false;

    ~Impl()
    {
        {
            std::lock_guard lock(state->mutex);
            state->stopped = true;
            state->sample.Reset();
        }
        if (reader && readStarted && SUCCEEDED(reader->Flush(MF_SOURCE_READER_ALL_STREAMS))) {
            std::unique_lock lock(state->mutex);
            state->ready.wait(lock, [&] {
                return state->flushed && state->active == 0;
            });
        }
        reader.Reset();
        if (source)
            source->Shutdown();
        avcodec_parameters_free(&parameters);
    }
};

MfCameraInput::MfCameraInput(const CameraSourceConfig &config) : m_impl(std::make_unique<Impl>())
{
    auto &input = *m_impl;
    if (!input.parameters)
        throw HRESULT(E_OUTOFMEMORY);
    input.source = openCamera(config.deviceId());
    ComPtr<IMFAttributes> attributes;
    check(MFCreateAttributes(&attributes, 3));
    ComPtr<ReaderCallback> callback;
    callback.Attach(new ReaderCallback(input.state));
    check(attributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get()));
    check(attributes->SetUINT32(MF_READWRITE_DISABLE_CONVERTERS, TRUE));
    check(attributes->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE));
    check(MFCreateSourceReaderFromMediaSource(input.source.Get(), attributes.Get(), &input.reader));
    check(input.reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE));
    check(input.reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));
    // 自动模式优先当前原生模式；显式配置只协商匹配的类型。
    ComPtr<IMFMediaType> current;
    check(input.reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current));
    if (matches(config, readMode(current.Get())))
        input.type = current;
    else {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> candidate;
            HRESULT hr = input.reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i,
                                                          &candidate);
            if (hr == MF_E_NO_MORE_TYPES)
                break;
            check(hr);
            if (matches(config, readMode(candidate.Get())) &&
                SUCCEEDED(input.reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                            nullptr, candidate.Get()))) {
                input.type = candidate;
                break;
            }
        }
    }
    if (!input.type)
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    check(input.reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                            input.type.Get()));
    check(input.reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &input.type));
    input.mode = readMode(input.type.Get());
    if (!matches(config, input.mode))
        throw HRESULT(MF_E_INVALIDMEDIATYPE);
    cameraMfParameters(input.type.Get(), input.parameters);
}

MfCameraInput::~MfCameraInput() = default;

const CameraMode &MfCameraInput::mode() const
{
    return m_impl->mode;
}

const AVCodecParameters *MfCameraInput::parameters() const
{
    return m_impl->parameters;
}

bool MfCameraInput::read(AVPacket *packet, Sample &result, const std::atomic_bool &stop)
{
    auto &input = *m_impl;
    auto &state = *input.state;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!stop.load()) {
        {
            std::lock_guard lock(state.mutex);
            check(state.error);
            state.completed = false;
        }
        check(input.reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr,
                                       nullptr, nullptr));
        input.readStarted = true;
        ComPtr<IMFSample> sample;
        {
            std::unique_lock lock(state.mutex);
            while (!state.completed && !stop.load()) {
                state.ready.wait_for(lock, std::chrono::milliseconds(20));
                if (std::chrono::steady_clock::now() >= deadline)
                    throw HRESULT(HRESULT_FROM_WIN32(ERROR_TIMEOUT));
            }
            if (stop.load())
                return false;
            check(state.error);
            sample = std::move(state.sample);
            result.timestamp100ns = state.timestamp;
            result.arrivalNs = state.arrival;
        }
        if (!sample)
            continue; // 流时钟通知没有样本，不重新计算超时期限。
        if (input.parameters->codec_id == AV_CODEC_ID_RAWVIDEO) {
            result.pixels = cameraMfPixels(sample.Get(), input.type.Get(), result.inferred);
            return true;
        }
        cameraMfPacket(sample.Get(), packet, result.timestamp100ns,
                       input.parameters->codec_id == AV_CODEC_ID_MJPEG);
        return true;
    }
    return false;
}
}
#endif

QList<CameraDevice> cameraDevices(QString *error)
{
    if (error)
        error->clear();
    QList<CameraDevice> result;
#ifdef Q_OS_WIN
    try {
        MfRuntime runtime;
        ComPtr<IMFAttributes> attributes;
        check(MFCreateAttributes(&attributes, 1));
        check(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                  MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
        IMFActivate **devices = nullptr;
        UINT32 count = 0;
        check(MFEnumDeviceSources(attributes.Get(), &devices, &count));
        const auto release = qScopeGuard([&] {
            for (UINT32 i = 0; i < count; ++i)
                devices[i]->Release();
            CoTaskMemFree(devices);
        });
        for (UINT32 i = 0; i < count; ++i)
            result.append({stringAttribute(devices[i],
                                           MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK),
                           stringAttribute(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME)});
    } catch (HRESULT hr) {
        if (error)
            *error = cameraMfError(hr);
    }
#else
    if (error)
        *error = QObject::tr("摄像头采集目前仅支持 Windows。");
#endif
    return result;
}

QList<CameraMode> cameraModes(const QString &id, QString *error)
{
    if (error)
        error->clear();
    QList<CameraMode> result;
#ifdef Q_OS_WIN
    try {
        MfRuntime runtime;
        auto source = openCamera(id);
        const auto shutdown = qScopeGuard([&] {
            source->Shutdown();
        });
        auto handler = videoTypes(source.Get());
        DWORD count = 0;
        check(handler->GetMediaTypeCount(&count));
        for (DWORD i = 0; i < count; ++i) {
            ComPtr<IMFMediaType> type;
            check(handler->GetMediaTypeByIndex(i, &type));
            auto mode = readMode(type.Get());
            if (mode.isValid() && !std::any_of(result.cbegin(), result.cend(), [&](const auto &m) {
                    return m.sameFormat(mode);
                }))
                result.append(mode);
        }
        std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
            if (a.width != b.width)
                return a.width < b.width;
            if (a.height != b.height)
                return a.height < b.height;
            return qint64(a.frameRateNumerator) * b.frameRateDenominator <
                   qint64(b.frameRateNumerator) * a.frameRateDenominator;
        });
        if (result.isEmpty())
            throw HRESULT(MF_E_INVALIDMEDIATYPE);
    } catch (HRESULT hr) {
        if (error)
            *error = cameraMfError(hr);
    }
#else
    Q_UNUSED(id);
    if (error)
        *error = QObject::tr("摄像头采集目前仅支持 Windows。");
#endif
    return result;
}

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
