#pragma once

#include "proto/probe/window_truth.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct CursorTruth {
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        std::uint32_t delay_ms = 0;
        std::vector<std::uint8_t> over_white;
        std::vector<std::uint8_t> over_black;

        [[nodiscard]] bool operator==(const Frame&) const = default;
    };

    std::uint32_t flags = 0;
    std::uint64_t handle = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint64_t under = 0;
    std::uint32_t system_idc = 0;
    std::int32_t display_count = 0;
    bool clipped = false;
    WindowTruth::Rect clip{};
    std::uint64_t capture = 0;
    bool raw_mouse = false;
    std::uint32_t raw_flags = 0;
    std::uint64_t raw_target = 0;
    std::uint64_t recentring = 0;
    std::uint64_t put_back_by = 0;
    std::uint32_t put_back = 0;
    std::int32_t mouse_speed = 0;
    bool enhance_precision = false;
    std::uint32_t metric_width = 0;
    std::uint32_t metric_height = 0;
    std::vector<Frame> frames{};

    [[nodiscard]] bool shown() const noexcept {
        constexpr std::uint32_t kShowing = 1;
        constexpr std::uint32_t kSuppressed = 2;
        return (flags & kShowing) != 0 && (flags & kSuppressed) == 0 && handle != 0;
    }

    [[nodiscard]] bool operator==(const CursorTruth&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<CursorTruth> read(MessageReader& in);
};

}
