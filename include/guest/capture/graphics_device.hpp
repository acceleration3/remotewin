#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IInspectable;

namespace rwin::guest {

// The Direct3D device every window capture draws through.
//
// Windows.Graphics.Capture hands out frames as Direct3D surfaces, so
// there has to be a device for them to belong to. One is enough for the
// whole process: the frame pools are per window, the device they share is
// not, and creating one per window would cost a driver context each.
//
// Created without a swap chain and with BGRA support, which is what the
// capture pixel format needs.
class GraphicsDevice {
public:
    GraphicsDevice();
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;
    GraphicsDevice(GraphicsDevice&&) = delete;
    GraphicsDevice& operator=(GraphicsDevice&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return device_ != nullptr; }

    [[nodiscard]] ID3D11Device* device() const noexcept { return device_; }
    [[nodiscard]] ID3D11DeviceContext* context() const noexcept { return context_; }
    // The same device as a WinRT object, which is what a frame pool wants.
    [[nodiscard]] IInspectable* winrt_device() const noexcept { return winrt_device_; }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IInspectable* winrt_device_ = nullptr;
};

}  // namespace rwin::guest
