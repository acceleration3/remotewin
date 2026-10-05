#include "guest/capture/picture_edges.hpp"

#include "guest/capture/graphics_device.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <d3d11.h>
// clang-format on

#include <cstdint>

namespace rwin::guest {

namespace {

constexpr std::size_t kBytesPerPixel = 4;
constexpr std::size_t kAlpha = 3;

ID3D11Texture2D* staging(ID3D11Device* device, UINT width, UINT height) {
    D3D11_TEXTURE2D_DESC described{};
    described.Width = width;
    described.Height = height;
    described.MipLevels = 1;
    described.ArraySize = 1;
    described.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    described.SampleDesc.Count = 1;
    described.Usage = D3D11_USAGE_STAGING;
    described.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* made = nullptr;
    if (FAILED(device->CreateTexture2D(&described, nullptr, &made))) return nullptr;
    return made;
}

}  // namespace

PictureEdges::~PictureEdges() { release(); }

void PictureEdges::release() noexcept {
    if (rows_ != nullptr) rows_->Release();
    if (columns_ != nullptr) columns_->Release();
    rows_ = nullptr;
    columns_ = nullptr;
    size_ = {};
}

bool PictureEdges::fit(CaptureStream::Extent size) {
    if (size == size_ && rows_ != nullptr && columns_ != nullptr) return true;
    release();
    rows_ = staging(device_.device(), size.width, 2);
    columns_ = staging(device_.device(), 2, size.height);
    if (rows_ == nullptr || columns_ == nullptr) {
        RWIN_WARN("shared", "no staging texture for a {}x{} picture's edges", size.width, size.height);
        release();
        return false;
    }
    size_ = size;
    return true;
}

bool PictureEdges::all_drawn(const CaptureStream::Frame& frame) {
    const CaptureStream::Extent size = frame.content();
    if (!fit(size)) return false;
    ID3D11DeviceContext* context = device_.context();
    const D3D11_BOX top{0, 0, 0, size.width, 1, 1};
    const D3D11_BOX bottom{0, size.height - 1, 0, size.width, size.height, 1};
    const D3D11_BOX left{0, 0, 0, 1, size.height, 1};
    const D3D11_BOX right{size.width - 1, 0, 0, size.width, size.height, 1};
    context->CopySubresourceRegion(rows_, 0, 0, 0, 0, frame.texture(), 0, &top);
    context->CopySubresourceRegion(rows_, 0, 0, 1, 0, frame.texture(), 0, &bottom);
    context->CopySubresourceRegion(columns_, 0, 0, 0, 0, frame.texture(), 0, &left);
    context->CopySubresourceRegion(columns_, 0, 1, 0, 0, frame.texture(), 0, &right);
    return rows_drawn() && columns_drawn();
}

bool PictureEdges::rows_drawn() {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_.context()->Map(rows_, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    bool drawn[2] = {false, false};
    for (std::size_t row = 0; row < 2; ++row) {
        const std::uint8_t* line = pixels + row * mapped.RowPitch;
        for (std::uint32_t x = 0; x < size_.width && !drawn[row]; ++x) drawn[row] = line[x * kBytesPerPixel + kAlpha] != 0;
    }
    device_.context()->Unmap(rows_, 0);
    return drawn[0] && drawn[1];
}

bool PictureEdges::columns_drawn() {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(device_.context()->Map(columns_, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    bool drawn[2] = {false, false};
    for (std::uint32_t y = 0; y < size_.height && !(drawn[0] && drawn[1]); ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * mapped.RowPitch;
        drawn[0] = drawn[0] || row[kAlpha] != 0;
        drawn[1] = drawn[1] || row[kBytesPerPixel + kAlpha] != 0;
    }
    device_.context()->Unmap(columns_, 0);
    return drawn[0] && drawn[1];
}

}  // namespace rwin::guest
