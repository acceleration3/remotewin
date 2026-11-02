#include "guest/capture/graphics_device.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <inspectable.h>
#include <roapi.h>
// clang-format on

namespace rwin::guest {

namespace {
extern "C" HRESULT WINAPI CreateDirect3D11DeviceFromDXGIDevice(IDXGIDevice*,
                                                              IInspectable**);

}

GraphicsDevice::GraphicsDevice() {
    const HRESULT initialised = ::RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialised) && initialised != RPC_E_CHANGED_MODE) {
        RWIN_ERROR("capture", "RoInitialize failed: {:#010x}",
                   static_cast<unsigned>(initialised));
        return;
    }

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                        D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    HRESULT created = ::D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_, nullptr, &context_);
    if (FAILED(created)) {
        created = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                      D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                      ARRAYSIZE(levels), D3D11_SDK_VERSION, &device_,
                                      nullptr, &context_);
        if (SUCCEEDED(created)) {
            RWIN_INFO("capture", "no hardware Direct3D; capturing through WARP");
        }
    }
    if (FAILED(created)) {
        RWIN_ERROR("capture", "no Direct3D device: {:#010x}",
                   static_cast<unsigned>(created));
        return;
    }

    IDXGIDevice* dxgi = nullptr;
    if (FAILED(device_->QueryInterface(__uuidof(IDXGIDevice),
                                       reinterpret_cast<void**>(&dxgi)))) {
        RWIN_ERROR("capture", "the Direct3D device is not a DXGI device");
        return;
    }
    constexpr INT kHighestGpuPriority = 7;
    if (FAILED(dxgi->SetGPUThreadPriority(kHighestGpuPriority))) {
        RWIN_DEBUG("capture", "the GPU will not put this device's work first");
    }
    const HRESULT wrapped = CreateDirect3D11DeviceFromDXGIDevice(dxgi, &winrt_device_);
    dxgi->Release();
    if (FAILED(wrapped)) {
        RWIN_ERROR("capture", "cannot wrap the device for capture: {:#010x}",
                   static_cast<unsigned>(wrapped));
        return;
    }
    RWIN_INFO("capture", "Direct3D device ready");
}

GraphicsDevice::~GraphicsDevice() {
    if (winrt_device_ != nullptr) winrt_device_->Release();
    if (context_ != nullptr) context_->Release();
    if (device_ != nullptr) device_->Release();
}

}
