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
    for (Edges& edges : edges_) {
        if (edges.rows != nullptr) edges.rows->Release();
        if (edges.columns != nullptr) edges.columns->Release();
        edges = Edges{};
    }
    size_ = {};
    verdict_.reset();
    unread_ = 0;
}

// A new size is a new picture: what the old one's edges said is not its.
bool PictureEdges::fit(CaptureStream::Extent size) {
    if (size == size_ && edges_[0].rows != nullptr) return true;
    release();
    for (Edges& edges : edges_) {
        edges.rows = staging(device_.device(), size.width, 2);
        edges.columns = staging(device_.device(), 2, size.height);
        if (edges.rows == nullptr || edges.columns == nullptr) {
            RWIN_WARN("shared", "no staging texture for a {}x{} picture's edges", size.width, size.height);
            release();
            return false;
        }
    }
    size_ = size;
    return true;
}

void PictureEdges::copy(Edges& edges, const CaptureStream::Frame& frame) {
    ID3D11DeviceContext* context = device_.context();
    const D3D11_BOX top{0, 0, 0, size_.width, 1, 1};
    const D3D11_BOX bottom{0, size_.height - 1, 0, size_.width, size_.height, 1};
    const D3D11_BOX left{0, 0, 0, 1, size_.height, 1};
    const D3D11_BOX right{size_.width - 1, 0, 0, size_.width, size_.height, 1};
    context->CopySubresourceRegion(edges.rows, 0, 0, 0, 0, frame.texture(), 0, &top);
    context->CopySubresourceRegion(edges.rows, 0, 0, 1, 0, frame.texture(), 0, &bottom);
    context->CopySubresourceRegion(edges.columns, 0, 0, 0, 0, frame.texture(), 0, &left);
    context->CopySubresourceRegion(edges.columns, 0, 1, 0, 0, frame.texture(), 0, &right);
    edges.copied = true;
}

bool PictureEdges::all_drawn(const CaptureStream::Frame& frame) {
    if (!fit(frame.content())) return false;
    Edges& current = edges_[next_];
    Edges& previous = edges_[1 - next_];
    next_ = 1 - next_;
    copy(current, frame);
    // The frame before's edges, if the GPU is done with them; the newest
    // ones, waited for, when nothing has said anything for too long.
    const bool overdue = !verdict_.has_value() || unread_ >= kMostUnread;
    std::optional<bool> heard = previous.copied ? read(previous, false) : std::nullopt;
    if (!heard.has_value() && overdue) heard = read(current, true);
    if (!heard.has_value()) {
        ++unread_;
        return verdict_.value_or(false);
    }
    verdict_ = heard;
    unread_ = 0;
    return *verdict_;
}

std::optional<bool> PictureEdges::read(Edges& edges, bool wait) {
    const std::optional<bool> rows = rows_drawn(edges.rows, wait);
    if (!rows.has_value()) return std::nullopt;
    const std::optional<bool> columns = columns_drawn(edges.columns, wait);
    if (!columns.has_value()) return std::nullopt;
    edges.copied = false;
    return *rows && *columns;
}

// A map that fails for any other reason than the copy still running says
// "not drawn": the frame is then read back whole, which is never wrong.
std::optional<bool> PictureEdges::rows_drawn(ID3D11Texture2D* rows, bool wait) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result = device_.context()->Map(rows, 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING) return std::nullopt;
    if (FAILED(result)) return false;
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    bool drawn[2] = {false, false};
    for (std::size_t row = 0; row < 2; ++row) {
        const std::uint8_t* line = pixels + row * mapped.RowPitch;
        for (std::uint32_t x = 0; x < size_.width && !drawn[row]; ++x) drawn[row] = line[x * kBytesPerPixel + kAlpha] != 0;
    }
    device_.context()->Unmap(rows, 0);
    return drawn[0] && drawn[1];
}

std::optional<bool> PictureEdges::columns_drawn(ID3D11Texture2D* columns, bool wait) {
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT result =
        device_.context()->Map(columns, 0, D3D11_MAP_READ, wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (result == DXGI_ERROR_WAS_STILL_DRAWING) return std::nullopt;
    if (FAILED(result)) return false;
    const auto* pixels = static_cast<const std::uint8_t*>(mapped.pData);
    bool drawn[2] = {false, false};
    for (std::uint32_t y = 0; y < size_.height && !(drawn[0] && drawn[1]); ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * mapped.RowPitch;
        drawn[0] = drawn[0] || row[kAlpha] != 0;
        drawn[1] = drawn[1] || row[kBytesPerPixel + kAlpha] != 0;
    }
    device_.context()->Unmap(columns, 0);
    return drawn[0] && drawn[1];
}

}  // namespace rwin::guest
