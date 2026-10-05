#pragma once

#include "guest/windows/work_area.hpp"

#include <cstdint>

namespace rwin::guest {

// Gives a guest window the size the host asked for, within what the guest
// has room for.
//
// The size is the picture's (WindowSurface), which is what the host sees;
// the window rectangle around it is that plus the invisible border.
//
// The screen is the ceiling: a window larger than the desktop has parts
// no capture can show, so a size beyond the work area is cut down to it.
// The host is told the same limit and stops the drag there itself, so
// what arrives here is rarely over it — the clamp is the guest holding
// its own line, not a negotiation.
//
// The window keeps its corner where it is, which is where the mirror is on
// the host (PlacementSync) - unless the new size would hang past the room,
// when it is moved in by as much as it would: every size the screen allows
// is reachable, and the window moves no further than that takes. Centred,
// as it once was, a window jumped across the guest's screen at every
// resize, away from where its mirror was.
class WindowResizer {
public:
    void resize(std::uint64_t window_handle, std::uint32_t width,
                std::uint32_t height) const;

private:
    WorkArea room_;
};

}  // namespace rwin::guest
