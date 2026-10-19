#include "host/placement/placement_sync.hpp"

#include "host/latency/frame_stamps.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "logging/logger.hpp"
#include "proto/rect.hpp"

#include <algorithm>

namespace rwin::host {

namespace {

// A report this soon after this side placed a window, of a place it did not
// ask for, is KWin finishing its own placement of it: a user's drag is
// reported only once it ends, and nobody drags a window that has just
// appeared within a second of it appearing.
constexpr std::int64_t kCompositorAfterPlacementNs = 1'000'000'000;

}  // namespace

PlacementSync::PlacementSync(EventLoop& loop, BusConnection& bus, GuestLink& link) : link_(link), kwin_(loop, bus) {
    kwin_.window_placed.connect([this](std::uint64_t key, PointF corner, std::int64_t) { placed(key, corner); });
    kwin_.window_not_placed.connect([this](std::uint64_t key) { not_placed(key); });
    kwin_.window_moved.connect([this](std::uint64_t key, const BoxF& frame) { moved(key, frame); });
}

std::string PlacementSync::tag_for(WindowId window) const {
    return active() ? KWinPlacement::tag_for(window.key()) : std::string();
}

void PlacementSync::follow_guest(const GuestWindow& window, std::uint32_t generation, Size logical, bool placeable) {
    const Point guest = window.picture_origin();
    Tracked& tracked = tracked_[window.id().key()];
    tracked.frame_size = window.frame().size();
    tracked.picture_offset = window.picture_offset();
    // The first surface is the one the first placement waits for.
    const bool remade = tracked.generation != 0 && tracked.generation != generation;
    const bool moved_in_guest = remade || !tracked.asked.has_value() || guest != tracked.guest;
    tracked.guest = guest;
    tracked.generation = generation;
    tracked.placeable = placeable;
    RWIN_TRACE("placement", "{} at {},{}: active {} placeable {} moved {}", window.id(), guest.x, guest.y, active(),
               placeable, moved_in_guest);
    if (!active() || !placeable || !moved_in_guest) return;
    // Where the guest went because it was moved from here: already there.
    if (!remade && tracked.awaiting_guest == guest) {
        tracked.awaiting_guest.reset();
        return;
    }
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) {
        tracked.asked.reset();
        return;
    }
    if (!remade && tracked.reported.has_value() && screen->guest_point(tracked.reported->corner()) == guest) return;
    const PointF corner = screen->host_point(guest);
    const BoxF frame{corner.x, corner.y, static_cast<double>(logical.width), static_cast<double>(logical.height)};
    RWIN_DEBUG("placement", "{} to {:.2f},{:.2f} {}x{} (surface {}{})", window.id(), frame.x, frame.y, logical.width,
               logical.height, generation, remade ? ", new" : "");
    tracked.asked = corner;
    tracked.asked_size = logical;
    tracked.recently_asked.push_back(guest);
    if (tracked.recently_asked.size() > kRecentlyAsked) tracked.recently_asked.pop_front();
    tracked.pending = true;
    tracked.placed_once = false;
    tracked.put_back = false;
    kwin_.place(window.id().key(), frame);
}

void PlacementSync::keep_on_guest_screen(WindowId window, std::uint32_t generation) {
    if (!active()) return;
    Tracked& tracked = tracked_[window.key()];
    if (tracked.on_guest_screen == generation) return;
    const std::string output = kwin_.primary_output_name();
    if (output.empty()) return;
    tracked.on_guest_screen = generation;
    tracked.pending = true;
    RWIN_DEBUG("placement", "{} maximised on the guest's screen, {}", window, output);
    kwin_.send_to_output(window.key(), output);
}

void PlacementSync::forget(WindowId window) { tracked_.erase(window.key()); }

void PlacementSync::placed(std::uint64_t key, PointF frame_corner) {
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return;
    Tracked& tracked = it->second;
    tracked.pending = false;
    tracked.placed_once = true;
    tracked.placed_at = now_ns();
    BoxF frame = tracked.reported.value_or(BoxF{});
    frame.x = frame_corner.x;
    frame.y = frame_corner.y;
    tracked.reported = frame;
}

void PlacementSync::not_placed(std::uint64_t key) {
    const auto it = tracked_.find(key);
    if (it != tracked_.end()) it->second.pending = false;
}

// Not while a placement of this side's is under way: the compositor's own
// first place for a new window comes before it, and is nobody's choice.
void PlacementSync::moved(std::uint64_t key, const BoxF& frame) {
    const WindowId window = WindowId::of_key(key);
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return;
    Tracked& tracked = it->second;
    tracked.reported = frame;
    // The window has come to KWin and the placement waiting for it did not
    // see it come - KWin can map a window between a script's look at its
    // windows and its waiting for more. It is there now.
    if (tracked.pending && tracked.asked.has_value() && !tracked.placed_once) {
        tracked.placed_once = true;
        kwin_.place(key, BoxF{tracked.asked->x, tracked.asked->y, static_cast<double>(tracked.asked_size.width),
                              static_cast<double>(tracked.asked_size.height)});
        return;
    }
    if (tracked.pending || !tracked.placeable) return;
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) return;
    const Point guest = screen->guest_point(frame.corner());
    if (tracked.asked.has_value() && !tracked.put_back && tracked.placed_at != 0
        && now_ns() - tracked.placed_at < kCompositorAfterPlacementNs && guest != screen->guest_point(*tracked.asked)) {
        RWIN_INFO("placement", "{} put at {:.2f},{:.2f} by KWin as it was placed; put back", window, frame.x, frame.y);
        tracked.put_back = true;
        kwin_.place(key, BoxF{tracked.asked->x, tracked.asked->y, static_cast<double>(tracked.asked_size.width),
                              static_cast<double>(tracked.asked_size.height)});
        return;
    }
    if (guest == tracked.guest || guest == tracked.awaiting_guest) return;
    if (std::find(tracked.recently_asked.begin(), tracked.recently_asked.end(), guest) != tracked.recently_asked.end()) {
        RWIN_DEBUG("placement", "{} reported at {},{}, where it was put a moment ago; not sent back", window, guest.x,
                   guest.y);
        return;
    }
    if (tracked.frame_size.empty()) return;
    tracked.awaiting_guest = guest;
    RWIN_DEBUG("placement", "{} put at {:.2f},{:.2f} here; guest window to {},{}", window, frame.x, frame.y, guest.x,
               guest.y);
    const Point corner = guest - tracked.picture_offset;
    link_.send_window_move(window, Rect{corner.x, corner.y, static_cast<std::uint32_t>(tracked.frame_size.width),
                                        static_cast<std::uint32_t>(tracked.frame_size.height)});
}

}  // namespace rwin::host
