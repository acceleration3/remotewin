#include "guest/capture/surface_ring.hpp"

#include "guest/capture/graphics_device.hpp"
#include "guest/capture/surface_id_reader.hpp"
#include "logging/logger.hpp"
#include "proto/slot_free.hpp"

// clang-format off
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
// clang-format on

#include <cassert>
#include <utility>

namespace rwin::guest {

SurfaceRing::SurfaceRing(const GraphicsDevice& device, const SurfaceIdReader& ids, std::uint64_t window,
                         std::uint32_t& rings_made) noexcept
    : device_(device), ids_(ids), window_(window), rings_made_(rings_made) {}

SurfaceRing::~SurfaceRing() { release_textures(); }

// Let go of, a ring's textures are let go of everywhere - nothing else in
// the guest opened them - and the GPU device tells the host so. Whatever
// the host still draws from them it keeps: what it imported names memory,
// not the textures.
void SurfaceRing::release_textures() noexcept {
    for (auto& source : sources_) source.reset();
    for (ID3D11Texture2D*& texture : textures_) {
        if (texture != nullptr) texture->Release();
        texture = nullptr;
    }
}

bool SurfaceRing::has_free_slot() const noexcept {
    return textures_[0] == nullptr || ledger_.free_slot().has_value();
}

std::optional<SurfaceRing::Delivery> SurfaceRing::copy(CaptureStream::Frame& frame) {
    const CaptureStream::Extent size = frame.content();
    Delivery delivery;
    if (textures_[0] == nullptr || size != CaptureStream::Extent{announced_.width, announced_.height}) {
        if (!remake(size)) return std::nullopt;
        delivery.made = announced_;
    }
    const std::optional<std::uint32_t> slot = ledger_.free_slot();
    assert(slot.has_value());
    if (!slot.has_value()) return std::nullopt;

    // The picture only: the frame's buffer is the pool's size, which is
    // larger than the picture while a window shrinks.
    const D3D11_BOX picture{0, 0, 0, size.width, size.height, 1};
    device_.context()->CopySubresourceRegion(textures_[*slot], 0, 0, 0, 0, frame.texture(), 0, &picture);
    // Sent now rather than at the device's leisure: the host is about to
    // be told this copy is coming, and asks the GPU device until it ran.
    device_.context()->Flush();
    // Flush submits the copy; it does not complete it. Keep WGC's pool buffer
    // out of reuse until the host's fenced release proves the copy finished.
    sources_[*slot] = std::move(frame);
    delivery.frame = FrameInSlot{window_, ledger_.ring(), *slot, ledger_.filled(*slot)};
    return delivery;
}

void SurfaceRing::free(const SlotFree& freed) noexcept {
    if (freed.window != window_) return;
    if (!ledger_.freed(freed.ring, freed.slot)) {
        RWIN_DEBUG("shared", "{:#x}: slot {} of ring {} was not the host's (ring {} now)", window_, freed.slot,
                   freed.ring, ledger_.ring());
        return;
    }
    sources_[freed.slot].reset();
}

bool SurfaceRing::remake(CaptureStream::Extent size) {
    release_textures();
    D3D11_TEXTURE2D_DESC described{};
    described.Width = size.width;
    described.Height = size.height;
    described.MipLevels = 1;
    described.ArraySize = 1;
    described.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    described.SampleDesc.Count = 1;
    described.Usage = D3D11_USAGE_DEFAULT;
    described.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    described.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    SlotRing made{.window = window_, .ring = rings_made_ + 1, .width = size.width, .height = size.height};
    for (std::size_t slot = 0; slot < textures_.size(); ++slot) {
        if (const HRESULT created = device_.device()->CreateTexture2D(&described, nullptr, &textures_[slot]);
            FAILED(created)) {
            RWIN_WARN("shared", "{:#x}: no shared {}x{} texture: {:#010x}", window_, size.width, size.height,
                      static_cast<unsigned>(created));
            release_textures();
            return false;
        }
        IDXGIResource* resource = nullptr;
        HANDLE shared = nullptr;
        const bool handed = SUCCEEDED(textures_[slot]->QueryInterface(__uuidof(IDXGIResource),
                                                                      reinterpret_cast<void**>(&resource)))
                            && SUCCEEDED(resource->GetSharedHandle(&shared));
        if (resource != nullptr) resource->Release();
        const std::optional<std::uint64_t> id = handed ? ids_.id_of(shared) : std::nullopt;
        if (!id.has_value()) {
            RWIN_WARN("shared", "{:#x}: shared texture {} has {}", window_, slot,
                      handed ? "no surface id" : "no shared handle");
            release_textures();
            return false;
        }
        made.surfaces[slot] = *id;
    }
    rings_made_ = made.ring;
    announced_ = made;
    ledger_ = SlotLedger{made.ring};
    RWIN_DEBUG("shared", "{:#x}: ring {} of {}x{}: {:#x} {:#x} {:#x}", window_, made.ring, size.width, size.height,
               made.surfaces[0], made.surfaces[1], made.surfaces[2]);
    return true;
}

}  // namespace rwin::guest
