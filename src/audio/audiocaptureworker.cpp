#include "audio/audiocaptureworker.h"
#include "audio/audioresampler.h"
#include <QScopeGuard>
#include <chrono>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace {
QString audioError(const QString &operation, HRESULT result, bool retry = true)
{
    return QStringLiteral("%1（0x%2）。")
               .arg(operation)
               .arg(quint32(result), 8, 16, QLatin1Char('0')) +
           (retry ? QStringLiteral("等待原设备恢复。") : QStringLiteral("请刷新设备列表。"));
}
}
#endif

QList<AudioDevice> audioDevices(QString *error)
{
    QList<AudioDevice> result;
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        *error = audioError(QStringLiteral("无法初始化音频设备查询"), initialized, false);
        return result;
    }
    const auto uninitialize = qScopeGuard([] {
        CoUninitialize();
    });
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
        *error = audioError(QStringLiteral("无法枚举音频设备"), hr, false);
        return result;
    }
    for (const auto flow : {eCapture, eRender}) {
        ComPtr<IMMDeviceCollection> devices;
        hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices);
        if (FAILED(hr)) {
            *error = audioError(QStringLiteral("无法读取音频设备列表"), hr, false);
            return {};
        }
        UINT count = 0;
        devices->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            ComPtr<IMMDevice> device;
            ComPtr<IPropertyStore> properties;
            LPWSTR id = nullptr;
            if (FAILED(devices->Item(index, &device)) || FAILED(device->GetId(&id)))
                continue;
            const auto freeId = qScopeGuard([&] {
                CoTaskMemFree(id);
            });
            QString name = QString::fromWCharArray(id);
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) &&
                    value.vt == VT_LPWSTR)
                    name = QString::fromWCharArray(value.pwszVal);
                PropVariantClear(&value);
            }
            result.append(
                {QString::fromWCharArray(id), name,
                 flow == eCapture ? AudioSourceType::Microphone : AudioSourceType::SystemAudio});
        }
    }
#else
    *error = QStringLiteral("声音采集目前仅支持 Windows。");
#endif
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
    m_stop.store(true);
    m_wake.notify_all();
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
    setPriority(QThread::HighPriority);
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        setStatus("error", audioError(QStringLiteral("无法初始化音频采集"), initialized));
        return;
    }
    const auto uninitialize = qScopeGuard([] {
        CoUninitialize();
    });
    while (!m_stop.load()) {
        QString error;
        try {
            error = capture();
        } catch (const std::exception &exception) {
            error = QStringLiteral("音频采集失败：%1").arg(QString::fromUtf8(exception.what()));
        }
        m_queue->clear();
        if (m_stop.load())
            break;
        setStatus("error", error);
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::seconds(1), [this] {
            return m_stop.load();
        });
    }
#else
    setStatus("error", QStringLiteral("声音采集目前仅支持 Windows。"));
#endif
    m_queue->clear();
}

QString AudioCaptureWorker::capture()
{
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE") == m_deviceId)
        return audioError(QStringLiteral("音频设备已中断"), AUDCLNT_E_DEVICE_INVALIDATED);
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
        return audioError(QStringLiteral("无法访问音频设备"), hr);
    ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(reinterpret_cast<LPCWSTR>(m_deviceId.utf16()), &device);
    if (FAILED(hr))
        return audioError(QStringLiteral("所选音频设备不可用"), hr);
    ComPtr<IMMEndpoint> endpoint;
    EDataFlow flow = eAll;
    if (FAILED(device.As(&endpoint)) || FAILED(endpoint->GetDataFlow(&flow)) ||
        flow != (m_type == AudioSourceType::Microphone ? eCapture : eRender))
        return QStringLiteral("音频设备类型不匹配。");
    ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
    if (FAILED(hr))
        return audioError(QStringLiteral("无法打开音频设备"), hr);
    WAVEFORMATEX *format = nullptr;
    hr = client->GetMixFormat(&format);
    if (FAILED(hr))
        return audioError(QStringLiteral("无法读取音频格式"), hr);
    const auto freeFormat = qScopeGuard([&] {
        CoTaskMemFree(format);
    });
    WORD formatTag = format->wFormatTag;
    quint64 channelMask = 0;
    if (formatTag == WAVE_FORMAT_EXTENSIBLE &&
        format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
        const auto *extended = reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format);
        channelMask = extended->dwChannelMask;
        if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
            formatTag = WAVE_FORMAT_IEEE_FLOAT;
        else if (extended->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
            formatTag = WAVE_FORMAT_PCM;
    }
    PcmFormat pcmFormat;
    if (formatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32)
        pcmFormat = PcmFormat::Float32;
    else if (formatTag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 64)
        pcmFormat = PcmFormat::Float64;
    else if (formatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 8)
        pcmFormat = PcmFormat::Unsigned8;
    else if (formatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16)
        pcmFormat = PcmFormat::Signed16;
    else if (formatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 24)
        pcmFormat = PcmFormat::Signed24;
    else if (formatTag == WAVE_FORMAT_PCM && format->wBitsPerSample == 32)
        pcmFormat = PcmFormat::Signed32;
    else
        return QStringLiteral("不支持设备提供的 PCM 音频格式。");
    if (format->nBlockAlign != format->nChannels * format->wBitsPerSample / 8)
        return QStringLiteral("音频设备提供了无效的采样对齐。");
    AudioResampler resampler;
    QString error;
    if (!resampler.configure(int(format->nSamplesPerSec), format->nChannels, pcmFormat, channelMask,
                             &error))
        return error;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                            m_type == AudioSourceType::SystemAudio ? AUDCLNT_STREAMFLAGS_LOOPBACK
                                                                   : 0,
                            1000000, 0, format, nullptr);
    if (FAILED(hr))
        return audioError(QStringLiteral("无法启动共享音频采集"), hr);
    ComPtr<IAudioCaptureClient> captureClient;
    hr = client->GetService(IID_PPV_ARGS(&captureClient));
    if (FAILED(hr))
        return audioError(QStringLiteral("无法获取音频采样服务"), hr);
    if (m_stop.load())
        return {};
    hr = client->Start();
    if (FAILED(hr))
        return audioError(QStringLiteral("无法开始音频采集"), hr);
    const auto stopClient = qScopeGuard([&] {
        client->Stop();
    });
    m_queue->clear();
    setStatus("capturing");
    qint64 originNs = 0, producedAudioFrames = 0, lastCorrectionNs = 0,
           lastStateCheckNs = audioClockNs();
    while (!m_stop.load()) {
        if (qEnvironmentVariable("OPENLIVE_FORCE_AUDIO_DEVICE_FAILURE") == m_deviceId)
            return audioError(QStringLiteral("音频设备已中断"), AUDCLNT_E_DEVICE_INVALIDATED);
        UINT32 availablePacketFrames = 0;
        hr = captureClient->GetNextPacketSize(&availablePacketFrames);
        if (FAILED(hr))
            return audioError(QStringLiteral("音频设备采集已中断"), hr);
        for (int packet = 0; availablePacketFrames && packet < 128 && !m_stop.load(); ++packet) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 deviceFramePosition = 0, deviceTimestamp100ns = 0;
            hr = captureClient->GetBuffer(&data, &frames, &flags, &deviceFramePosition,
                                          &deviceTimestamp100ns);
            if (FAILED(hr))
                return audioError(QStringLiteral("无法读取音频采样"), hr);
            if (hr == AUDCLNT_S_BUFFER_EMPTY)
                break;
            QByteArray pcmBytes;
            HRESULT released = S_OK;
            {
                const auto release = qScopeGuard([&] {
                    released = captureClient->ReleaseBuffer(frames);
                });
                if (frames > format->nSamplesPerSec)
                    return QStringLiteral("音频设备返回了过大的采样块。");
                if (frames && !data && !(flags & AUDCLNT_BUFFERFLAGS_SILENT))
                    return QStringLiteral("音频设备返回了空采样缓冲。");
                const int bytes = int(frames * format->nBlockAlign);
                pcmBytes = flags & AUDCLNT_BUFFERFLAGS_SILENT
                          ? QByteArray(bytes, 0)
                          : QByteArray(reinterpret_cast<const char *>(data), bytes);
                // 无符号 PCM 的静音值是中点；填零会产生直流偏移。
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) && pcmFormat == PcmFormat::Unsigned8)
                    pcmBytes.fill(char(128));
            }
            if (FAILED(released))
                return audioError(QStringLiteral("无法归还音频采样缓冲"), released);
            if (frames) {
                const qint64 timestampNs =
                    !(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) && deviceTimestamp100ns
                        ? qint64(deviceTimestamp100ns) * 100
                        : audioClockNs() - qint64(frames) * 1000000000LL / format->nSamplesPerSec;
                const qint64 expectedTimestampNs =
                    originNs + AudioFormat::framesToNs(producedAudioFrames) +
                    resampler.delayFrames() * 1000000000LL / format->nSamplesPerSec;
                if (!originNs || (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ||
                    std::abs(timestampNs - expectedTimestampNs) > 50000000LL) {
                    if (!resampler.reset())
                        return QStringLiteral("无法重建音频重采样器。");
                    originNs = timestampNs;
                    producedAudioFrames = 0;
                    lastCorrectionNs = timestampNs;
                    m_queue->clear();
                } else if (timestampNs - lastCorrectionNs >= 1000000000LL) {
                    const int correctionFrames = int(std::clamp<qint64>(
                        AudioFormat::nsToFrames(timestampNs - expectedTimestampNs), -48, 48));
                    if (!resampler.compensate(correctionFrames, AudioFormat::rate))
                        return QStringLiteral("无法校正音频设备时钟。");
                    lastCorrectionNs = timestampNs;
                }
                auto samples =
                    resampler.convert(reinterpret_cast<const unsigned char *>(pcmBytes.constData()),
                                      int(frames), &error);
                if (!error.isEmpty())
                    return error;
                if (!samples.isEmpty()) {
                    AudioBlock block{originNs + AudioFormat::framesToNs(producedAudioFrames),
                                     std::move(samples)};
                    producedAudioFrames += block.frames();
                    m_queue->publish(std::move(block));
                }
            }
            hr = captureClient->GetNextPacketSize(&availablePacketFrames);
            if (FAILED(hr))
                return audioError(QStringLiteral("音频采集已中断"), hr);
        }
        const auto now = audioClockNs();
        if (now - lastStateCheckNs > 1000000000LL) {
            DWORD state = 0;
            hr = device->GetState(&state);
            if (FAILED(hr) || state != DEVICE_STATE_ACTIVE)
                return audioError(QStringLiteral("所选音频设备已断开"),
                                  FAILED(hr) ? hr : AUDCLNT_E_DEVICE_INVALIDATED);
            lastStateCheckNs = now;
        }
        std::unique_lock<std::mutex> lock(m_mutex);
        m_wake.wait_for(lock, std::chrono::milliseconds(5), [this] {
            return m_stop.load();
        });
    }
    return {};
#else
    return QStringLiteral("声音采集目前仅支持 Windows。");
#endif
}
