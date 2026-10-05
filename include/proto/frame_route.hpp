#pragma once

#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// How the guest's frames come to the host (FrameRoute, host to guest).
//
// As pixels (Frame, FramePatch) unless the host says it can take shared
// surfaces: it has the GPU device's export socket and renders on the GPU
// the guest's surfaces live on. Then a window's frames stay on the GPU -
// copied into a ring of surfaces the host imports (SlotRing, FrameInSlot,
// SlotFree) - and nothing but their names crosses the wire. Said again,
// off, when the host can no longer take them; the guest then goes back to
// pixels for every window.
struct FrameRoute {
    bool shared_surfaces = false;

    [[nodiscard]] bool operator==(const FrameRoute&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<FrameRoute> read(MessageReader& in);
};

}  // namespace rwin
