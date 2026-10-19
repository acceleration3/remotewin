#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "proto/system_cursor.hpp"

#include <cstdint>
#include <vector>

namespace rwin::host {

// A guest cursor as this side keeps it (from a CursorPicture): which system
// cursor it is, if any, and its pictures in guest pixels, which are this
// screen's device pixels.
struct CursorLook {
    struct Frame {
        Size size;
        // Straight-alpha BGRA, top-down, width * 4 per row.
        std::vector<std::uint8_t> bgra;
        Point hotspot;
        int delay_ms = 0;
    };

    std::uint64_t id = 0;
    SystemCursor system = SystemCursor::None;
    bool inverts = false;
    std::vector<Frame> frames;

    [[nodiscard]] bool animated() const noexcept { return frames.size() > 1; }
};

}  // namespace rwin::host
