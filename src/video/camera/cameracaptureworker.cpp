#include "video/camera/cameracaptureworker.h"
#include <QElapsedTimer>
#include <QScopeGuard>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT hr)
{
    if (FAILED(hr))
        throw hr;
}

QString failure(HRESULT hr)
{
    return QObject::tr("摄像头操作失败（0x%1），请检查设备连接、占用情况和相机访问权限。")
        .arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

struct Runtime
{
    bool com = false, mf = false;

    Runtime()
    {
        check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        com = true;
        const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        if (FAILED(hr)) {
            CoUninitialize();
            com = false;
            throw hr;
        }
        mf = true;
    }

    ~Runtime()
    {
        if (mf)
            MFShutdown();
        if (com)
            CoUninitialize();
    }
};

QString stringAttribute(IMFAttributes *attrs, REFGUID key)
{
    WCHAR *value = nullptr;
    check(attrs->GetAllocatedString(key, &value, nullptr));
    const QString result = QString::fromWCharArray(value);
    CoTaskMemFree(value);
    return result;
}

ComPtr<IMFMediaSource> openCamera(const QString &deviceId)
{
    ComPtr<IMFAttributes> attrs;
    check(MFCreateAttributes(&attrs, 2));
    check(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                         MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    check(attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                           reinterpret_cast<LPCWSTR>(deviceId.utf16())));
    ComPtr<IMFMediaSource> source;
    check(MFCreateDeviceSource(attrs.Get(), &source));
    return source;
}

CameraMode modeInfo(IMFMediaType *type)
{
    UINT32 width = 0, height = 0, numerator = 0, denominator = 0;
    GUID subtype{};
    if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
        FAILED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &numerator, &denominator)) || !width ||
        !height || width > INT_MAX / 4 || height > INT_MAX || !numerator || !denominator ||
        numerator > INT_MAX || denominator > INT_MAX ||
        FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)))
        return {};
    WCHAR guid[40];
    StringFromGUID2(subtype, guid, 40);
    return {int(width),
            int(height),
            int(numerator),
            int(denominator),
            QString::fromWCharArray(guid),
            QStringLiteral("%1 × %2 · %3 FPS")
                .arg(width)
                .arg(height)
                .arg(double(numerator) / denominator, 0, 'f', denominator == 1 ? 0 : 2)};
}

bool matches(const CameraSourceParams &params, const CameraMode &mode)
{
    if (params.resolution.isEmpty())
        return true;
    const double wanted = double(params.frameRateNumerator) / params.frameRateDenominator;
    const double actual = double(mode.numerator) / mode.denominator;
    return params.resolution == QSize(mode.width, mode.height) &&
           std::abs(wanted - actual) <= std::max(0.001, wanted * 0.0001);
}

struct Samples
{
    std::mutex mutex;
    std::mutex requests;
    std::condition_variable ready;
    ComPtr<IMFSourceReader> reader;
    ComPtr<IMFSample> latest;
    HRESULT error = S_OK;
    bool stopped = false;
    bool flushed = false;
    int callbacks = 0;
    LONGLONG timestamp = 0;
    qint64 arrivalNs = 0;
};

class Callback final : public IMFSourceReaderCallback
{
    std::atomic<ULONG> refs{1};
    std::shared_ptr<Samples> state;

public:
    explicit Callback(std::shared_ptr<Samples> value) : state(std::move(value))
    {
    }

    STDMETHODIMP QueryInterface(REFIID iid, void **out) override
    {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMFSourceReaderCallback)) {
            *out = static_cast<IMFSourceReaderCallback *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const ULONG n = --refs;
        if (!n)
            delete this;
        return n;
    }

    STDMETHODIMP OnReadSample(HRESULT hr, DWORD, DWORD flags, LONGLONG timestamp,
                              IMFSample *sample) override
    {
        {
            std::lock_guard lock(state->mutex);
            ++state->callbacks;
        }
        const auto finished = qScopeGuard([&] {
            {
                std::lock_guard lock(state->mutex);
                --state->callbacks;
            }
            state->ready.notify_one();
        });
        {
            std::lock_guard lock(state->mutex);
            if (state->stopped)
                return S_OK;
            if (FAILED(hr))
                state->error = hr;
            else if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                state->error = MF_E_END_OF_STREAM;
            else if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
                state->error = MF_E_INVALIDMEDIATYPE;
            else if (sample) {
                state->latest = sample;
                state->timestamp = timestamp;
                state->arrivalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
            }
        }
        state->ready.notify_one();
        // Serialize submission with stop/Flush: no request may slip in after cancellation.
        std::lock_guard request(state->requests);
        ComPtr<IMFSourceReader> reader;
        {
            std::lock_guard lock(state->mutex);
            if (!state->stopped && SUCCEEDED(state->error))
                reader = state->reader;
        }
        if (reader) {
            const HRESULT next = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                                    nullptr, nullptr, nullptr);
            if (FAILED(next)) {
                std::lock_guard lock(state->mutex);
                if (!state->stopped)
                    state->error = next;
                state->ready.notify_one();
            }
        }
        return S_OK;
    }

    STDMETHODIMP OnFlush(DWORD) override
    {
        {
            std::lock_guard lock(state->mutex);
            state->flushed = true;
        }
        state->ready.notify_one();
        return S_OK;
    }

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *) override
    {
        return S_OK;
    }
};

QImage copyRgb(IMFSample *sample, int width, int height, LONG stride)
{
    ComPtr<IMFMediaBuffer> buffer;
    check(sample->ConvertToContiguousBuffer(&buffer));
    ComPtr<IMF2DBuffer> plane;
    BYTE *bytes = nullptr;
    if (SUCCEEDED(buffer.As(&plane))) {
        check(plane->Lock2D(&bytes, &stride));
        const auto unlock = qScopeGuard([&] {
            plane->Unlock2D();
        });
        if (std::abs(qint64(stride)) < qint64(width) * 4)
            throw E_INVALIDARG;
        QImage image(width, height, QImage::Format_RGB32);
        if (image.isNull())
            throw E_OUTOFMEMORY;
        for (int y = 0; y < height; ++y)
            memcpy(image.scanLine(y), bytes + qint64(y) * stride, size_t(width) * 4);
        return image;
    }
    DWORD length = 0;
    check(buffer->Lock(&bytes, nullptr, &length));
    const auto unlock = qScopeGuard([&] {
        buffer->Unlock();
    });
    if (std::abs(qint64(stride)) < qint64(width) * 4 ||
        qint64(length) < std::abs(qint64(stride)) * height)
        throw E_INVALIDARG;
    if (stride < 0)
        bytes += std::abs(qint64(stride)) * (height - 1);
    QImage image(width, height, QImage::Format_RGB32);
    if (image.isNull())
        throw E_OUTOFMEMORY;
    for (int y = 0; y < height; ++y)
        memcpy(image.scanLine(y), bytes + qint64(y) * stride, size_t(width) * 4);
    return image;
}
}
#endif

QList<CameraDevice> cameraDevices(QString *error)
{
    QList<CameraDevice> result;
#ifdef Q_OS_WIN
    try {
        Runtime runtime;
        ComPtr<IMFAttributes> attrs;
        check(MFCreateAttributes(&attrs, 1));
        check(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
        IMFActivate **devices = nullptr;
        UINT32 count = 0;
        check(MFEnumDeviceSources(attrs.Get(), &devices, &count));
        const auto cleanup = qScopeGuard([&] {
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
            *error = failure(hr);
    }
#else
    if (error)
        *error = QObject::tr("摄像头采集仅支持 Windows。");
#endif
    return result;
}

QList<CameraMode> cameraModes(const QString &deviceId, QString *error)
{
    QList<CameraMode> result;
#ifdef Q_OS_WIN
    try {
        Runtime runtime;
        auto source = openCamera(deviceId);
        const auto shutdown = qScopeGuard([&] {
            source->Shutdown();
        });
        ComPtr<IMFSourceReader> reader;
        check(MFCreateSourceReaderFromMediaSource(source.Get(), nullptr, &reader));
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> type;
            const HRESULT hr =
                reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type);
            if (hr == MF_E_NO_MORE_TYPES)
                break;
            check(hr);
            auto mode = modeInfo(type.Get());
            if (!mode.isValid())
                continue;
            const bool duplicate =
                std::any_of(result.cbegin(), result.cend(), [&](const CameraMode &other) {
                    return other.sameFormat(mode);
                });
            if (!duplicate)
                result.append(mode);
        }
    } catch (HRESULT hr) {
        if (error)
            *error = failure(hr);
    }
#else
    Q_UNUSED(deviceId);
    if (error)
        *error = QObject::tr("摄像头采集仅支持 Windows。");
#endif
    return result;
}

CameraCaptureWorker::CameraCaptureWorker(CameraSourceParams params, quint64 generation)
    : m_params(std::move(params)), m_generation(generation)
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
    QString error;
#ifdef Q_OS_WIN
    try {
        Runtime runtime;
        auto source = openCamera(m_params.deviceId);
        const auto shutdown = qScopeGuard([&] {
            source->Shutdown();
        });
        auto samples = std::make_shared<Samples>();
        ComPtr<Callback> callback;
        callback.Attach(new Callback(samples));
        ComPtr<IMFAttributes> attrs;
        check(MFCreateAttributes(&attrs, 3));
        check(attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.Get()));
        check(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
        check(attrs->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE));
        ComPtr<IMFSourceReader> reader;
        check(MFCreateSourceReaderFromMediaSource(source.Get(), attrs.Get(), &reader));
        bool readStarted = false;
        const auto stopReader = qScopeGuard([&] {
            {
                std::lock_guard request(samples->requests);
                std::lock_guard lock(samples->mutex);
                samples->stopped = true;
            }
            // Flush is asynchronous with this reader. Keep COM/MF and the source alive
            // until OnFlush and all in-flight sample callbacks have drained.
            if (readStarted && SUCCEEDED(reader->Flush(MF_SOURCE_READER_ALL_STREAMS))) {
                std::unique_lock lock(samples->mutex);
                samples->ready.wait(lock, [&] {
                    return samples->flushed && samples->callbacks == 0;
                });
            }
            {
                std::lock_guard lock(samples->mutex);
                samples->reader.Reset();
                samples->latest.Reset();
            }
        });
        check(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE));
        check(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));
        ComPtr<IMFMediaType> output;
        check(MFCreateMediaType(&output));
        check(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
        check(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
        if (!m_params.resolution.isEmpty()) {
            bool selected = false;
            for (DWORD i = 0;; ++i) {
                ComPtr<IMFMediaType> type;
                const HRESULT hr =
                    reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &type);
                if (hr == MF_E_NO_MORE_TYPES)
                    break;
                check(hr);
                if (!matches(m_params, modeInfo(type.Get())))
                    continue;
                if (SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                          nullptr, type.Get())) &&
                    SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                          nullptr, output.Get()))) {
                    selected = true;
                    break;
                }
            }
            if (!selected) {
                error = tr("设备不支持所选分辨率和帧率，请修改采集模式。");
                throw MF_E_INVALIDMEDIATYPE;
            }
        } else
            check(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr,
                                              output.Get()));
        check(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &output));
        const auto actual = modeInfo(output.Get());
        if (!actual.isValid() || !matches(m_params, actual)) {
            error = tr("设备实际采用的采集模式与所选模式不符。");
            throw MF_E_INVALIDMEDIATYPE;
        }
        const int width = actual.width, height = actual.height;
        UINT32 encodedStride = 0;
        LONG stride = width * 4;
        if (SUCCEEDED(output->GetUINT32(MF_MT_DEFAULT_STRIDE, &encodedStride)))
            stride = LONG(encodedStride);
        else
            check(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride));
        {
            std::lock_guard lock(samples->mutex);
            samples->reader = reader;
        }
        if (m_stop.load())
            return;
        check(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr,
                                 nullptr));
        readStarted = true;
        QElapsedTimer lastFrame;
        lastFrame.start();
        const double fps = double(actual.numerator) / double(actual.denominator);
        bool received = false;
        quint64 sequence = 0;
        // MF provides device timestamps; publish a steady-clock timestamp too for future
        // common-timeline mapping.
        try {
            while (!m_stop.load()) {
                ComPtr<IMFSample> sample;
                LONGLONG timestamp = 0;
                qint64 arrivalNs = 0;
                {
                    std::unique_lock lock(samples->mutex);
                    samples->ready.wait_for(lock, std::chrono::milliseconds(20), [&] {
                        return samples->latest || FAILED(samples->error);
                    });
                    check(samples->error);
                    sample = std::move(samples->latest);
                    timestamp = samples->timestamp;
                    arrivalNs = samples->arrivalNs;
                }
                if (!sample) {
                    if (lastFrame.elapsed() > std::max(received ? 5000.0 : 10000.0, 3000.0 / fps)) {
                        error = received ? tr("摄像头停止出帧，请检查连接后重试。")
                                         : tr("等待摄像头首帧超时，请重试。");
                        break;
                    }
                    continue;
                }
                auto frame = std::make_shared<CameraFrame>();
                frame->image = copyRgb(sample.Get(), width, height, stride);
                frame->timestampNs = arrivalNs;
                frame->deviceTimestamp100ns = timestamp;
                frame->sequence = ++sequence;
                frame->generation = m_generation;
                frame->rateNumerator = actual.numerator;
                frame->rateDenominator = actual.denominator;
                {
                    QMutexLocker lock(&m_mutex);
                    m_latest = std::move(frame);
                }
                received = true;
                lastFrame.restart();
            }
            if (!error.isEmpty())
                fail(error);
        } catch (HRESULT hr) {
            // Publish the fault before potentially slow driver teardown, so the GUI
            // clears stale video while this worker is being retired in the background.
            fail(failure(hr));
            throw;
        } catch (...) {
            fail(tr("摄像头帧处理失败，请重试。"));
            throw;
        }
    } catch (HRESULT hr) {
        if (error.isEmpty())
            error = failure(hr);
    } catch (...) {
        error = tr("摄像头帧处理失败，请重试。");
    }
#else
    error = tr("摄像头采集仅支持 Windows。");
#endif
    if (!error.isEmpty())
        fail(error);
}
