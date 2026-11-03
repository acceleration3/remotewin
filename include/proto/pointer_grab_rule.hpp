#pragma once

#include "proto/pointer_grab.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {
class PointerGrabRule {
public:
    struct Point {
        std::int32_t x = 0;
        std::int32_t y = 0;
        [[nodiscard]] bool operator==(const Point&) const noexcept = default;
    };

    struct Look {
        WindowId foreground;
        bool cursor_shown = true;
        std::optional<PointerGrab::Area> clip;
        Point cursor;
        std::uint64_t now_ms = 0;
        bool button_held = false;
    };

    static constexpr std::int32_t kSmallClip = 16;
    static constexpr int kReturnsToWarp = 3;
    static constexpr std::uint64_t kReturnGapMs = 1000;
    static constexpr std::uint64_t kSettleMs = 25;
    static constexpr int kFreeMovesToStop = 2;
    static constexpr std::int32_t kWarpDistance = 32;

    void put_at(Point at, std::uint64_t now_ms) noexcept;
    void nudged(Point from, std::uint64_t now_ms) noexcept;

    [[nodiscard]] PointerGrab grab_for(const Look& look) noexcept;

    [[nodiscard]] bool warping() const noexcept { return warping_; }

private:
    struct Pending {
        Point point;
        bool absolute = false;
        std::uint64_t since_ms = 0;
    };

    void judge(const Look& look) noexcept;
    void note_return(Point at, std::uint64_t now_ms) noexcept;
    void note_free() noexcept;
    void forget() noexcept;

    std::optional<Pending> pending_;
    WindowId window_;
    Point return_point_;
    int returns_ = 0;
    std::uint64_t last_return_ms_ = 0;
    int free_moves_ = 0;
    bool warping_ = false;
    std::optional<Point> held_at_;
    bool dragged_away_ = false;
};

}
