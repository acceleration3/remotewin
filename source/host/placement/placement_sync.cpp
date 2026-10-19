#include "host/placement/placement_sync.hpp"

#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/mirror/pixel_mapping.hpp"
#include "logging/logger.hpp"
#include "proto/rect.hpp"

#include <algorithm>
#include <QGuiApplication>
#include <QScreen>


namespace rwin::host {

namespace {

// A report this soon after this side placed a window, of a place it did not
// ask for, is KWin finishing its own placement of it: a user's drag is
// reported only once it ends, and nobody drags a window that has just
// appeared within a second of it appearing.
constexpr qint64 kCompositorAfterPlacementMs = 1000;

}  // namespace

PlacementSync::PlacementSync(GuestLink& link, QObject* parent) : QObject(parent), link_(link) {
    connect(&kwin_, &KWinPlacement::window_placed, this, &PlacementSync::placed);
    connect(&kwin_, &KWinPlacement::window_not_placed, this, &PlacementSync::not_placed);
    connect(&kwin_, &KWinPlacement::window_moved, this, &PlacementSync::moved);
}

std::optional<ScreenMapping> PlacementSync::guest_screen() const {
    if (!active()) return std::nullopt;
    const QScreen* primary = QGuiApplication::primaryScreen();
    return kwin_.output_named(primary != nullptr ? primary->name() : QString());
}

QString PlacementSync::tag_for(WindowId window) const {
    return active() ? KWinPlacement::tag_for(window.key()) : QString();
}

void PlacementSync::follow_guest(MirrorWindow& mirror, const GuestWindow& window, bool placeable) {
    const QRect guest(window.picture_origin(), window.picture_size());
    Tracked& tracked = tracked_[mirror.window().key()];
    tracked.mirror = &mirror;
    tracked.frame_size = window.frame().size();
    tracked.picture_offset = window.picture_origin() - window.frame().topLeft();
    // The first surface is the one the first placement waits for.
    const bool remade = tracked.surface != 0 && tracked.surface != mirror.surface_generation();
    const bool moved_in_guest = remade || !tracked.asked.has_value() || guest.topLeft() != tracked.guest;
    tracked.guest = guest.topLeft();
    tracked.surface = mirror.surface_generation();
    tracked.placeable = placeable;
    if (!active() || !placeable || !moved_in_guest) return;
    // Where the guest went because it was moved from here: already there.
    if (!remade && tracked.awaiting_guest == guest.topLeft()) {
        tracked.awaiting_guest.reset();
        return;
    }
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) {
        tracked.asked.reset();
        return;
    }
    if (!remade && tracked.reported.has_value() && screen->guest_point(tracked.reported->topLeft()) == guest.topLeft()) {
        return;
    }
    // The size the mirror is, one to one at the output's scale
    // (PixelMapping): whole logical units, as the mirror sizes itself.
    const QRectF frame(screen->host_point(guest.topLeft()),
                       QSizeF(PixelMapping{screen->scale()}.logical_size(guest.size())));
    RWIN_DEBUG("placement", "{} to {:.2f},{:.2f} {:.0f}x{:.0f} (surface {}{})", mirror.window(), frame.x(),
               frame.y(), frame.width(), frame.height(), tracked.surface, remade ? ", new" : "");
    tracked.asked = frame.topLeft();
    tracked.asked_size = frame.size();
    tracked.recently_asked.push_back(guest.topLeft());
    if (tracked.recently_asked.size() > kRecentlyAsked) tracked.recently_asked.pop_front();
    tracked.pending = true;
    tracked.placed_once = false;
    tracked.put_back = false;
    ++tracked.requests;
    kwin_.place(mirror.window().key(), frame);
}

void PlacementSync::keep_on_guest_screen(MirrorWindow& mirror) {
    if (!active()) return;
    Tracked& tracked = tracked_[mirror.window().key()];
    tracked.mirror = &mirror;
    if (tracked.on_guest_screen == mirror.surface_generation()) return;
    const QScreen* primary = QGuiApplication::primaryScreen();
    if (primary == nullptr) return;
    tracked.on_guest_screen = mirror.surface_generation();
    tracked.pending = true;
    RWIN_DEBUG("placement", "{} maximised on the guest's screen, {}", mirror.window(), primary->name().toStdString());
    kwin_.send_to_output(mirror.window().key(), primary->name());
}

void PlacementSync::forget(WindowId window) { tracked_.remove(window.key()); }

void PlacementSync::placed(std::uint64_t key, QPointF frame_corner, qint64 round_trip_ms) {
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return;
    it->pending = false;
    it->placed_once = true;
    it->round_trip_ms = round_trip_ms;
    it->since_placed.start();
    QRectF frame = it->reported.value_or(QRectF());
    frame.moveTopLeft(frame_corner);
    it->reported = frame;
}

void PlacementSync::not_placed(std::uint64_t key) {
    const auto it = tracked_.find(key);
    if (it != tracked_.end()) it->pending = false;
}

// Not while a placement of this side's is under way: the compositor's own
// first place for a new window comes before it, and is nobody's choice.
void PlacementSync::moved(std::uint64_t key, QRectF frame) {
    const WindowId window = WindowId::of_key(key);
    const auto it = tracked_.find(key);
    if (it == tracked_.end()) return;
    it->reported = frame;
    if (it->mirror.isNull()) return;
    // The window has come to KWin and the placement waiting for it did not
    // see it come - KWin can map a window between a script's look at its
    // windows and its waiting for more. It is there now.
    if (it->pending && it->asked.has_value() && !it->placed_once) {
        it->placed_once = true;
        kwin_.place(key, QRectF(*it->asked, it->asked_size));
        return;
    }
    if (it->pending || !it->placeable) return;
    const std::optional<ScreenMapping> screen = guest_screen();
    if (!screen.has_value()) return;
    const QPoint guest = screen->guest_point(frame.topLeft());
    if (it->asked.has_value() && !it->put_back && it->since_placed.isValid()
        && !it->since_placed.hasExpired(kCompositorAfterPlacementMs) && guest != screen->guest_point(*it->asked)) {
        RWIN_INFO("placement", "{} put at {:.2f},{:.2f} by KWin as it was placed; put back", window, frame.x(),
                  frame.y());
        it->put_back = true;
        kwin_.place(key, QRectF(*it->asked, it->asked_size));
        return;
    }
    if (guest == it->guest || guest == it->awaiting_guest) return;
    if (std::find(it->recently_asked.begin(), it->recently_asked.end(), guest) != it->recently_asked.end()) {
        RWIN_DEBUG("placement", "{} reported at {},{}, where it was put a moment ago; not sent back", window,
                   guest.x(), guest.y());
        return;
    }
    if (it->frame_size.isEmpty()) return;
    it->moved_guest_to = guest;
    it->awaiting_guest = guest;
    ++it->guest_moves;
    RWIN_DEBUG("placement", "{} put at {:.2f},{:.2f} here; guest window to {},{}", window, frame.x(),
               frame.y(), guest.x(), guest.y());
    const QPoint corner = guest - it->picture_offset;
    link_.send_window_move(window, Rect{corner.x(), corner.y(), static_cast<std::uint32_t>(it->frame_size.width()),
                                        static_cast<std::uint32_t>(it->frame_size.height())});
}

void PlacementSync::describe(WindowId window, MirrorState& state) const {
    const auto it = tracked_.constFind(window.key());
    if (it == tracked_.cend()) return;
    state.placement_asked = it->asked;
    state.placement_requests = it->requests;
    state.placement_round_trip_ms = it->round_trip_ms;
    state.compositor_frame = it->reported;
    state.guest_moves = it->guest_moves;
    state.guest_moved_to = it->moved_guest_to;
}

}  // namespace rwin::host
