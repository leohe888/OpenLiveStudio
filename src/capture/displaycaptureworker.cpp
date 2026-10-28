#include "displaycaptureworker.h"
#include "displaytargets.h"
#include "d3dtexture.h"
#include <QScopeGuard>
#include <QDebug>
#include <array>
#include <algorithm>
#include <chrono>

#ifdef Q_OS_WIN
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

using Microsoft::WRL::ComPtr;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
#endif

DisplayCaptureWorker::DisplayCaptureWorker(QString displayId, bool cursor, quint64 generation)
    : m_displayId(std::move(displayId)), m_cursor(cursor), m_generation(generation)
{
}

DisplayCaptureWorker::~DisplayCaptureWorker()
{
    stop();
    wait(); // Only called after finished during normal operation; shutdown joins remaining workers.
}

QString DisplayCaptureWorker::error() const
{
    std::lock_guard<std::mutex> lock(m_errorMutex);
    return m_error;
}

void DisplayCaptureWorker::run()
{
    const auto fail = [&](const QString &message) {
        m_frames.clear();
        std::lock_guard<std::mutex> lock(m_errorMutex);
        m_error = message;
    };
#ifdef Q_OS_WIN
    try {
        if (m_stop.load()) return;
        HMONITOR monitor = nullptr;
        for (const auto &target : displayTargets())
            if (target.id == m_displayId) monitor = reinterpret_cast<HMONITOR>(target.monitor);
        if (!monitor) { fail(tr("所选显示器已断开，请在来源属性中重新选择或重试。")); return; }
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        const auto apartment = qScopeGuard([] {
            // The last worker MTA must not leave cached factories pointing into unloaded COM servers.
            winrt::clear_factory_cache();
            winrt::uninit_apartment();
        });
        if (!GraphicsCaptureSession::IsSupported()) {
            fail(tr("当前系统不支持 Windows Graphics Capture。"));
            return;
        }
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
        ComPtr<IDXGIDevice> dxgi;
        winrt::check_hresult(device.As(&dxgi));
        winrt::com_ptr<IInspectable> inspectable;
        winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.Get(), inspectable.put()));
        auto directDevice = inspectable.as<IDirect3DDevice>();
        auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        GraphicsCaptureItem item{nullptr};
        winrt::check_hresult(interop->CreateForMonitor(monitor, winrt::guid_of<GraphicsCaptureItem>(), winrt::put_abi(item)));
        auto size = item.Size();
        auto pool = Direct3D11CaptureFramePool::CreateFreeThreaded(directDevice,
            DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
        auto session = pool.CreateCaptureSession(item);
        const auto closeObject = [](auto &object) noexcept {
            try { object.Close(); }
            catch (const winrt::hresult_error &error) { qWarning() << "WGC cleanup HRESULT:" << error.code().value; }
        };
        const auto close = qScopeGuard([&] { closeObject(session); closeObject(pool); });
        session.IsCursorCaptureEnabled(m_cursor);
        auto closed = std::make_shared<std::atomic_bool>(false);
        auto token = item.Closed([closed](auto &&, auto &&) { closed->store(true); });
        const auto revoke = qScopeGuard([&] { item.Closed(token); });
        if (m_stop.load()) return;
        session.StartCapture();
        struct Slot {
            std::shared_ptr<GpuFrameResource> resource;
            std::shared_ptr<std::atomic_bool> busy = std::make_shared<std::atomic_bool>(false);
        };
        std::array<Slot, 3> poolSlots;
        ComPtr<ID3D11Query> query;
        D3D11_QUERY_DESC queryDesc{D3D11_QUERY_EVENT, 0};
        winrt::check_hresult(device->CreateQuery(&queryDesc, &query));
        quint64 sequence = 0;
        auto nextTargetCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!m_stop.load()) {
            // Polling coalesces frame notifications and bounds application processing to 30 Hz.
            const auto next = std::chrono::steady_clock::now() + std::chrono::microseconds(33334);
            if (closed->load()) { fail(tr("显示器采集已关闭，请手动重试。")); break; }
            if (std::chrono::steady_clock::now() >= nextTargetCheck) {
                const auto available = displayTargets();
                if (std::none_of(available.cbegin(), available.cend(), [&](const auto &target) { return target.id == m_displayId; })) {
                    fail(tr("所选显示器已断开，请在来源属性中重新选择或重试。"));
                    break;
                }
                nextTargetCheck = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            }
            Direct3D11CaptureFrame latest{nullptr};
            while (auto frame = pool.TryGetNextFrame()) {
                if (latest) latest.Close();
                latest = std::move(frame);
            }
            if (latest) {
                auto content = latest.ContentSize();
                if (content.Width != size.Width || content.Height != size.Height) {
                    size = content;
                    latest.Close();
                    latest = nullptr;
                    m_frames.clear();
                    poolSlots = {};
                    if (size.Width > 0 && size.Height > 0)
                        pool.Recreate(directDevice, DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
                } else if (content.Width > 0 && content.Height > 0) {
                    auto access = latest.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                    ComPtr<ID3D11Texture2D> input;
                    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(&input)));
                    auto frame = std::make_shared<CaptureFrame>();
                    frame->size = QSize(size.Width, size.Height);
                    frame->timestamp = latest.SystemRelativeTime().count() * 100;
                    frame->sequence = ++sequence;
                    frame->generation = m_generation;
                    D3D11_TEXTURE2D_DESC desc{};
                    input->GetDesc(&desc);
                    for (auto &slot : poolSlots) {
                        bool available = false;
                        if (!slot.busy->compare_exchange_strong(available, true)) continue;
                        frame->busy = slot.busy;
                        if (!slot.resource) {
                            slot.resource = std::make_shared<GpuFrameResource>();
                            slot.resource->device = device;
                            desc.Usage = D3D11_USAGE_DEFAULT;
                            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
                            desc.CPUAccessFlags = 0;
                            desc.MiscFlags = 0;
                            winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, &slot.resource->texture));
                        }
                        frame->gpu = slot.resource;
                        context->CopyResource(slot.resource->texture.Get(), input.Get());
                        context->End(query.Get());
                        context->Flush();
                        break;
                    }
                    if (frame->gpu) {
                        BOOL ready = FALSE;
                        HRESULT result = S_FALSE;
                        // The worker waits for GPU completion; the GUI never waits on this query.
                        while (!m_stop.load() && (result = context->GetData(query.Get(), &ready, sizeof(ready), 0)) == S_FALSE)
                            QThread::msleep(1);
                        winrt::check_hresult(result);
                        if (!m_stop.load() && ready) {
                            m_frames.publish(std::move(frame));
                        }
                    }
                    latest.Close();
                }
            }
            std::unique_lock<std::mutex> waitLock(m_waitMutex);
            m_wait.wait_until(waitLock, next, [this] { return m_stop.load(); });
        }
    } catch (const winrt::hresult_error &error) {
        fail(tr("显示器采集失败：%1（0x%2）。请手动重试。")
            .arg(QString::fromWCharArray(error.message().c_str()))
            .arg(quint32(error.code().value), 8, 16, QLatin1Char('0')));
    } catch (const std::exception &error) {
        fail(tr("显示器采集失败：%1").arg(QString::fromUtf8(error.what())));
    }
#else
    fail(tr("显示器采集目前仅支持 Windows。"));
#endif
}
