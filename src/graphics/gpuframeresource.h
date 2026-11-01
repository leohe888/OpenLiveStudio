#ifndef GPUFRAMERESOURCE_H
#define GPUFRAMERESOURCE_H

#ifdef Q_OS_WIN
#include <d3d11.h>
#include <wrl/client.h>

struct GpuFrameResource
{
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
};
#else
struct GpuFrameResource
{
};
#endif

#endif
