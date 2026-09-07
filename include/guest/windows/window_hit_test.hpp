#pragma once

#include "guest/windows/window_surface.hpp"

#include <cstdint>

namespace rwin::guest {

// What part of itself a window says a point is over.
//
// The answer comes from the window, not from measuring its border: an
// application that draws its own chrome — which most of them now do —
// puts its buttons wherever it likes, and only it knows where.
class WindowHitTest {
public:
    // A HT* code, or HTNOWHERE when the window did not answer in time. A
    // window busy in a modal loop must not be allowed to stall the
    // session waiting for it.
    [[nodiscard]] std::int32_t at(const WindowSurface& surface, std::int32_t window_x,
                                  std::int32_t window_y) const;
};

}  // namespace rwin::guest
