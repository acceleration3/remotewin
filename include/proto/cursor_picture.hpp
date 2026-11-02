#pragma once

#include "proto/system_cursor.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct CursorPicture {
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        std::uint32_t delay_ms = 0;
        std::vector<std::uint8_t> bgra;

        [[nodiscard]] bool operator==(const Frame&) const = default;
    };

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

}
