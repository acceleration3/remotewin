#pragma once

#include "proto/probe/window_truth.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// What Windows says about the cursor and the pointer, read in the probe:
// the cursor it shows and what that looks like, whether it shows one at
// all, and what the application did to the pointer - clipped it, captured
// it, asked for its raw motion.
struct CursorTruth {
    // One picture of the cursor (one per step of an animated one), as the
    // user sees it: drawn by DrawIconEx over white and over black, so a
    // monochrome cursor's inverting pixels are what they invert to.
    struct Frame {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t hotspot_x = 0;
        std::uint32_t hotspot_y = 0;
        // GetCursorFrameInfo's rate, in milliseconds; 0 for a still cursor.
        std::uint32_t delay_ms = 0;
        // BGRA, top-down, opaque.
        std::vector<std::uint8_t> over_white;
        std::vector<std::uint8_t> over_black;

        [[nodiscard]] bool operator==(const Frame&) const = default;
    };

    // GetCursorInfo: CURSOR_SHOWING (1), CURSOR_SUPPRESSED (2).
    std::uint32_t flags = 0;
    std::uint64_t handle = 0;
    // The cursor's place on the screen, physical pixels.
    std::int32_t x = 0;
    std::int32_t y = 0;
    // WindowFromPoint there.
    std::uint64_t under = 0;
    // The IDC_ number of the system cursor it is (LoadCursor's shared
    // handle), 0 for one an application made.
    std::uint32_t system_idc = 0;
    // ShowCursor's display counter on the probe's thread: the cursor is
    // hidden over its windows while it is below 0.
    std::int32_t display_count = 0;
    // GetClipCursor, and whether that is less than the whole desktop.
    bool clipped = false;
    WindowTruth::Rect clip{};
    // GetCapture on the probe's thread.
    std::uint64_t capture = 0;
    // GetRegisteredRawInputDevices for the mouse (usage page 1, usage 2).
    bool raw_mouse = false;
    std::uint32_t raw_flags = 0;
    std::uint64_t raw_target = 0;
    // The probe window that puts the cursor back at its middle on every
    // move (mouse look without raw input), 0 for none.
    std::uint64_t recentring = 0;
    // How many moves in a row a window has put back, as anyone watching the
    // cursor could have seen: counted from the last move it left where it
    // went, and kept when it stops putting it back until one is - the
    // window it was is ``put_back_by``.
    std::uint64_t put_back_by = 0;
    std::uint32_t put_back = 0;
    // The pointer's ballistics: SPI_GETMOUSESPEED (1-20, 10 is one to one)
    // and "Enhance pointer precision" (SPI_GETMOUSE's acceleration).
    std::int32_t mouse_speed = 0;
    bool enhance_precision = false;
    // SM_CXCURSOR, SM_CYCURSOR: the size system cursors are made at.
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

}  // namespace rwin
