#pragma once

#include <cstdint>

namespace rwin::guest {

class WindowSurface;

// Puts a guest window where the host put its mirror: its picture's corner
// (WindowSurface) at a point of the guest's screen, the invisible border
// round it where Windows wants it. Without activating it or changing its
// place in the z-order: the move is the host's, not the user's.
class WindowMover {
public:
    void move(const WindowSurface& surface, std::int32_t x, std::int32_t y) const;
};

}  // namespace rwin::guest
