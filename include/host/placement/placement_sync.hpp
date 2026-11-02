#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/box_f.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/loop/signal.hpp"
#include "host/placement/kwin_placement.hpp"
#include "host/placement/screen_mapping.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace rwin::host {

class BusConnection;
class GuestWindow;

class PlacementSync {
public:
    PlacementSync(EventLoop& loop, BusConnection& bus);

    [[nodiscard]] bool active() const noexcept { return kwin_.active(); }

    [[nodiscard]] std::string caption_tag(WindowId window, KWinPlacement::Skips skips) const;

    void follow_guest(const GuestWindow& window, std::uint32_t generation, bool placeable, bool answering);
    void keep_on_guest_screen(WindowId window, std::uint32_t generation);
    void forget(WindowId window);
    void hold_for_drag(WindowId window);
    void end_drag_hold(WindowId window);
    void guest_resized(WindowId window);

    void prefer_output(std::string name) { kwin_.prefer_output(std::move(name)); }

    Signal<WindowId, Point> moved_here;
    Signal<WindowId, bool> minimized_here;

private:
    struct Tracked {
        Point guest;
        std::uint32_t generation = 0;
        bool placeable = false;
        bool minimized = false;
        bool held = false;
        std::int64_t guest_resized_at = 0;
        bool pending = false;
        bool placed_once = false;
        std::optional<PointF> asked;
        std::optional<BoxF> reported;
        std::optional<Point> awaiting_guest;
        std::int64_t placed_at = 0;
        bool put_back = false;
        std::uint32_t on_guest_screen = 0;
        std::deque<Point> recently_asked;
    };

    static constexpr std::size_t kRecentlyAsked = 32;

    void placed(std::uint64_t key, PointF frame_corner);
    void not_placed(std::uint64_t key);
    void moved(std::uint64_t key, const KWinPlacement::Report& report);
    void place_at_guest(WindowId window, Tracked& tracked, bool remade);
    bool put_back(std::uint64_t key, Tracked& tracked, const KWinPlacement::Report& report, Point guest,
                  const std::optional<BoxF>& before);

    KWinPlacement kwin_;
    std::unordered_map<std::uint64_t, Tracked> tracked_;
};

}
