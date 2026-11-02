#pragma once

#include "guest/capture/capture_stream.hpp"
#include "guest/capture/slot_ledger.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/slot_ring.hpp"
#include "proto/window_id.hpp"

#include <array>
#include <cstdint>
#include <optional>

struct ID3D11Texture2D;

namespace rwin {
struct SlotFree;
}

namespace rwin::guest {

class GraphicsDevice;
class SurfaceIdReader;

class SurfaceRing {
public:
    struct Delivery {
        std::optional<SlotRing> made;
        FrameInSlot frame;
    };

    SurfaceRing(const GraphicsDevice& device, const SurfaceIdReader& ids, WindowId window,
                std::uint32_t& rings_made) noexcept;
    ~SurfaceRing();

    SurfaceRing(const SurfaceRing&) = delete;
    SurfaceRing& operator=(const SurfaceRing&) = delete;
    SurfaceRing(SurfaceRing&&) = delete;
    SurfaceRing& operator=(SurfaceRing&&) = delete;

    [[nodiscard]] bool has_free_slot() const noexcept;

    [[nodiscard]] std::optional<Delivery> copy(CaptureStream::Frame& frame);

    void free(const SlotFree& freed) noexcept;

private:
    [[nodiscard]] bool remake(CaptureStream::Extent size);
    void release_textures() noexcept;

    const GraphicsDevice& device_;
    const SurfaceIdReader& ids_;
    WindowId window_;
    std::uint32_t& rings_made_;
    std::array<ID3D11Texture2D*, SlotRing::kSlots> textures_{};
    std::array<std::optional<CaptureStream::Frame>, SlotRing::kSlots> sources_{};
    SlotRing announced_;
    SlotLedger ledger_{0};
};

}
