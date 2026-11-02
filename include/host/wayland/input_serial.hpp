#pragma once

#include <cstdint>

struct wl_surface;

namespace rwin::host {
struct InputSerial {
    std::uint32_t serial = 0;
    wl_surface* surface = nullptr;
    std::int64_t at = 0;

    [[nodiscard]] bool valid() const noexcept { return surface != nullptr; }
};

}
