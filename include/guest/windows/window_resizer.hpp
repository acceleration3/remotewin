#pragma once

#include <cstdint>

namespace rwin::guest {

// Gives a guest window the size the host asked for.
//
// Where the window already sits decides how big it can get: one whose
// left edge is most of the way across the guest screen has only the
// remainder to grow into, so a host mirror dragged wider stops short and
// springs back at whatever width the window happened to allow. Centring
// it on the screen first removes that limit — from the middle, every size
// up to the whole screen is reachable, whatever the window's position
// was.
//
// The screen is still the ceiling. A host display larger than the guest's
// cannot be filled by a guest window, so the size is clamped to what the
// guest has and the host follows the size it is told the window ended up
// with.
class WindowResizer {
public:
    void resize(std::uint64_t window_handle, std::uint32_t width,
                std::uint32_t height) const;
};

}  // namespace rwin::guest
