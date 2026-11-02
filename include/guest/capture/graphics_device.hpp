#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IInspectable;

namespace rwin::guest {
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
    [[nodiscard]] IInspectable* winrt_device() const noexcept { return winrt_device_; }

private:
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IInspectable* winrt_device_ = nullptr;
};

}
