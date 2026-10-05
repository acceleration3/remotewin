#pragma once

#include "proto/system_cursor.hpp"

#include <QImage>
#include <QPoint>

#include <cstdint>
#include <vector>

namespace rwin::host {

// A guest cursor as this side keeps it (from a CursorPicture): which system
// cursor it is, if any, and its pictures in guest pixels, which are this
// screen's device pixels.
struct CursorLook {
    struct Frame {
        // Straight alpha (Format_ARGB32).
        QImage image;
        QPoint hotspot;
        int delay_ms = 0;
    };

    std::uint64_t id = 0;
    SystemCursor system = SystemCursor::None;
    bool inverts = false;
    std::vector<Frame> frames;

    [[nodiscard]] bool animated() const noexcept { return frames.size() > 1; }
};

}  // namespace rwin::host
