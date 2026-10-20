#include "host/placement/placement_sync.hpp"

#include "host/latency/frame_stamps.hpp"
#include "host/link/guest_window.hpp"
#include "logging/logger.hpp"

#include <algorithm>

namespace rwin::host {

namespace {

// A report this soon after this side placed a window, of a place it did not
// ask for, is KWin finishing its own placement of it: a user's drag is
// reported only once it ends, and nobody drags a window that has just
// appeared within a second of it appearing.
constexpr std::int64_t kCompositorAfterPlacementNs = 1'000'000'000;
// KWin answers a client's own resize with a frame of that size at once.
constexpr std::int64_t kRegravitatedWithinNs = 1'000'000'000;

}  // namespace

PlacementSync::PlacementSync(EventLoop& loop, BusConnection& bus) : kwin_(loop, bus) {
    kwin_.window_placed.connect([this](std::uint64_t key, PointF corner, std::int64_t) { placed(key, corner); });
    kwin_.window_not_placed.connect([this](std::uint64_t key) { not_placed(key); });
    kwin_.window_moved.connect(
        [this](std::uint64_t key, const KWinPlacement::Report& report) { moved(key, report); });
}

std::string PlacementSync::caption_tag(WindowId window, KWinPlacement::Skips skips) const {
    return active() ? KWinPlacement::caption_tag(window.key(), skips) : std::string();
}

void PlacementSync::follow_guest(const GuestWindow& window, std::uint32_t generation, bool placeable,
                                 bool answering) {
    const Point guest = window.picture_origin();
    Tracked& tracked = tracked_[window.id().key()];
    // The first surface is the one the first placement waits for.
    const bool remade = tracked.generation != 0 && tracked.generation != generation;
    const bool moved_in_guest = remade || !tracked.asked.has_value() || guest != tracked.guest;
    tracked.guest = guest;
    tracked.generation = generation;
    tracked.placeable = placeable;
    RWIN_TRACE("placement", "{} at {},{}: active {} placeable {} moved {}{}{}", window.id(), guest.x, guest.y,
               active(), placeable, moved_in_guest, tracked.held ? ", dragged here" : "",
               answering ? ", answering" : "");
    if (!active() || !placeable || !moved_in_guest || tracked.held || answering) return;
    // Where the guest went because it was moved from here: already there.
    if (!remade && tracked.awaiting_guest == guest) {
        tracked.awaiting_guest.reset();
        return;
    }
    place_at_guest(window.id(), tracked, remade);
}

void PlacementSync::place_at_guest(WindowId window, Tracked& tracked, bool remade) {
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) {
        tracked.asked.reset();
        return;
    }
    const Point guest = tracked.guest;
    if (!remade && tracked.reported.has_value() && screen->guest_point(tracked.reported->corner()) == guest) return;
    const PointF corner = screen->host_point(guest);
    RWIN_DEBUG("placement", "{} to {:.2f},{:.2f} (surface {}{})", window, corner.x, corner.y, tracked.generation,
               remade ? ", new" : "");
    tracked.asked = corner;
    tracked.recently_asked.push_back(guest);
    if (tracked.recently_asked.size() > kRecentlyAsked) tracked.recently_asked.pop_front();
    tracked.pending = true;
    tracked.placed_once = false;
    tracked.put_back = false;
    kwin_.place(window.key(), corner);
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

void PlacementSync::hold_for_drag(WindowId window) {
    if (!active()) return;
    tracked_[window.key()].held = true;
    RWIN_DEBUG("placement", "{} dragged here: the guest window is not followed until the drag ends", window);
}

void PlacementSync::end_drag_hold(WindowId window) {
    const auto it = tracked_.find(window.key());
    if (it == tracked_.end() || !it->second.held) return;
    it->second.held = false;
    RWIN_DEBUG("placement", "{} pressed with no drag reported ended; followed again", window);
}

void PlacementSync::guest_resized(WindowId window) {
    const auto it = tracked_.find(window.key());
    if (it != tracked_.end()) it->second.guest_resized_at = now_ns();
}

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
// Not for a frame a state gave the window either - maximised, fullscreen,
// minimised - which is no place to move the guest window to.
void PlacementSync::moved(std::uint64_t key, const KWinPlacement::Report& report) {
    const WindowId window = WindowId::of_key(key);
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return;
    Tracked& tracked = it->second;
    const std::optional<BoxF> before = tracked.reported;
    tracked.reported = report.frame;
    if (report.dragged) tracked.held = false;
    if (report.minimized != tracked.minimized) {
        tracked.minimized = report.minimized;
        minimized_here(window, report.minimized);
    }
    // The window has come to KWin and the placement waiting for it did not
    // see it come - KWin can map a window between a script's look at its
    // windows and its waiting for more. It is there now.
    if (tracked.pending && tracked.asked.has_value() && !tracked.placed_once) {
        tracked.placed_once = true;
        kwin_.place(key, *tracked.asked);
        return;
    }
    if (tracked.pending || !tracked.placeable || report.by_state || report.minimized) return;
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) return;
    const Point guest = screen->guest_point(report.frame.corner());
    if (put_back(key, tracked, report, guest, *screen, before)) return;
    if (guest == tracked.guest || guest == tracked.awaiting_guest) return;
    const bool resized =
        before.has_value() && (before->width != report.frame.width || before->height != report.frame.height);
    if (!report.dragged && resized && now_ns() - std::exchange(tracked.guest_resized_at, 0) < kRegravitatedWithinNs) {
        RWIN_DEBUG("placement", "{} resized in the guest, put at {:.2f},{:.2f} by KWin; back to the guest's corner",
                   window, report.frame.x, report.frame.y);
        place_at_guest(window, tracked, false);
        return;
    }
    if (!report.dragged
        && std::find(tracked.recently_asked.begin(), tracked.recently_asked.end(), guest)
               != tracked.recently_asked.end()) {
        RWIN_DEBUG("placement", "{} reported at {},{}, where it was put a moment ago; not sent back", window, guest.x,
                   guest.y);
        return;
    }
    tracked.awaiting_guest = guest;
    RWIN_DEBUG("placement", "{} put at {:.2f},{:.2f} here; guest window to {},{}", window, report.frame.x,
               report.frame.y, guest.x, guest.y);
    moved_here(window, guest);
}

// KWin's own placement of a new window - on the output its user is on - can
// be reported after this side's: a move alone, of a window placed under a
// second ago, that its user did not drag.
bool PlacementSync::put_back(std::uint64_t key, Tracked& tracked, const KWinPlacement::Report& report, Point guest,
                             const ScreenMapping& screen, const std::optional<BoxF>& before) {
    const bool moved_alone = before.has_value() && before->width == report.frame.width
                             && before->height == report.frame.height;
    if (report.dragged || !moved_alone || !tracked.asked.has_value() || tracked.put_back || tracked.placed_at == 0
        || now_ns() - tracked.placed_at >= kCompositorAfterPlacementNs || guest == screen.guest_point(*tracked.asked)) {
        return false;
    }
    RWIN_INFO("placement", "{} put at {:.2f},{:.2f} by KWin as it was placed; put back", WindowId::of_key(key),
              report.frame.x, report.frame.y);
    tracked.put_back = true;
    kwin_.place(key, *tracked.asked);
    return true;
}

}  // namespace rwin::host
