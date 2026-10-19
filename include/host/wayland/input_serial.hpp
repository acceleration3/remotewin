#pragma once

#include <cstdint>

struct wl_surface;

namespace rwin::host {

// The serial of the user's latest press - a button or a key - and the
// surface it was on: what a compositor wants before it lets a client grab
// a popup, start a move, or hand activation on.
struct InputSerial {
    std::uint32_t serial = 0;
    wl_surface* surface = nullptr;
    // now_ns when it came.
    std::int64_t at = 0;

    [[nodiscard]] bool valid() const noexcept { return surface != nullptr; }
};

}  // namespace rwin::host
