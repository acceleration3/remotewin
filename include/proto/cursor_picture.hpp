#pragma once

#include "proto/system_cursor.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// A guest cursor as the host is sent it, once: which system cursor it is,
// if it is one, and its pictures - one for a still cursor, one per step for
// an animated one, each shown for its own time.
//
// A monochrome cursor may have pixels that invert whatever is under them
// (AND 1, XOR 1). Nothing composes a cursor that way on this side, so those
// pixels arrive opaque black with a white edge where they meet
// transparency - what they look like over the light background they were
// drawn for, and still visible over a dark one - and ``inverts`` says so.
struct CursorPicture {
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        // How long this picture is shown before the next; 0 for a still
        // cursor.
        std::uint32_t delay_ms = 0;
        // Straight BGRA, top-down, width * 4 per row.
        std::vector<std::uint8_t> bgra;

        [[nodiscard]] bool operator==(const Frame&) const = default;
    };

    // Larger than any cursor Windows makes (256 px, 128 steps), so a
    // malformed message cannot ask the host for gigabytes.
    static constexpr std::uint32_t kMaxSide = 1024;
    static constexpr std::uint32_t kMaxFrames = 512;

    std::uint64_t id = 0;
    SystemCursor system = SystemCursor::None;
    bool inverts = false;
    std::vector<Frame> frames;

    [[nodiscard]] bool operator==(const CursorPicture&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<CursorPicture> read(MessageReader& in);
};

}  // namespace rwin
