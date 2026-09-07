#pragma once

#include "guest/windows/work_area.hpp"

#include <cstdint>

namespace rwin::guest {

// Gives a guest window the size the host asked for, within what the guest
// has room for.
//
// The screen is the ceiling: a window larger than the desktop has parts
// no capture can show, so a size beyond the work area is cut down to it.
// The host is told the same limit and stops the drag there itself, so
// what arrives here is rarely over it — the clamp is the guest holding
// its own line, not a negotiation.
//
// Where the window already sits decides how big it can get: one whose
// left edge is most of the way across the screen has only the remainder
// to grow into. Centring it removes that limit — from the middle, every
// size the screen allows is reachable, whatever the window's position
// was.
class WindowResizer {
public:
    void resize(std::uint64_t window_handle, std::uint32_t width,
                std::uint32_t height) const;

private:
    WorkArea room_;
};

}  // namespace rwin::guest
