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

// One window's frames, kept on the GPU: a ring of shared textures the
// picture's size, which the host imports from the GPU device's export
// socket, and each frame copied into one the host is not reading.
//
// A frame costs one copy on the guest's GPU and a few bytes on the wire,
// where read back as pixels it was the whole picture through QEMU's user
// network - about 116 MB a second, eight frames of a 2560x1440 window.
//
// The textures are the kind the GPU device can hand to another process
// (accel-virt's docs/external-export.md 5.1): legacy shared, B8G8R8A8, one
// mip, default usage, bound to nothing but shading and rendering.
class SurfaceRing {
public:
    // What a copy is to tell the host: the ring first when it was made
    // afresh for this frame.
    struct Delivery {
        std::optional<SlotRing> made;
        FrameInSlot frame;
    };

    // ``rings_made`` numbers every ring of the session, so a ring's number
    // is never reused while the host may still free a slot of it.
    SurfaceRing(const GraphicsDevice& device, const SurfaceIdReader& ids, WindowId window,
                std::uint32_t& rings_made) noexcept;
    ~SurfaceRing();

    SurfaceRing(const SurfaceRing&) = delete;
    SurfaceRing& operator=(const SurfaceRing&) = delete;
    SurfaceRing(SurfaceRing&&) = delete;
    SurfaceRing& operator=(SurfaceRing&&) = delete;

    // Whether a frame taken now has somewhere to go. Asked before a frame
    // is taken from the capture: one taken and dropped is a picture lost,
    // and a window sitting still sends no other.
    [[nodiscard]] bool has_free_slot() const noexcept;

    // ``frame``'s picture copied into a free slot, the ring made afresh at
    // the picture's size first when that changed. Nothing when the shared
    // textures cannot be made or their ids read: this guest cannot share
    // frames, and they go as pixels.
    // Consumes the frame only after a successful copy submission.
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

}  // namespace rwin::guest
