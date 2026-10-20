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

namespace rwin::host {

class BusConnection;
class GuestWindow;

// Keeps a toplevel mirror where its guest window is, where the compositor
// can be told and asked (KWinPlacement): a window the guest moves -
// SetWindowPos, a dialog centred on its owner, CW_USEDEFAULT - is put at
// the same place on this desktop, and a mirror moved here - dragged by the
// user, placed by the compositor - has its guest window moved under it
// (moved_here), so the guest's own idea of where the window is (the
// monitor a dialog centres on) is what is on screen. A nicety for first
// placement and for keeping the two in step: nothing depends on it, input
// reaches a window wherever either side has it (R2). Elsewhere the
// compositor places mirrors and the guest is not told.
//
// Places only: a size is the client's to commit, and a frame the
// compositor has for a state - maximised, fullscreen - is no place.
class PlacementSync {
public:
    PlacementSync(EventLoop& loop, BusConnection& bus);

    [[nodiscard]] bool active() const noexcept { return kwin_.active(); }

    // What the mirror of ``window`` is known to the compositor by, and which
    // of its lists it is left out of: a suffix of its title, or nothing
    // where nothing is placed.
    [[nodiscard]] std::string caption_tag(WindowId window, KWinPlacement::Skips skips) const;

    // Where the guest window's picture is, as the guest last said, for a
    // toplevel mirror whose surface is the ``generation``-th made for it.
    // ``placeable`` is false for a window minimised or maximised in the
    // guest: those are placed by their state. A mirror whose surface was
    // made again is placed again: the compositor put the new one where it
    // chose. ``answering`` while the guest has moves of this side's still to
    // answer: where it says it is then, it is on its way to where it was
    // asked to be, and is not followed.
    void follow_guest(const GuestWindow& window, std::uint32_t generation, bool placeable, bool answering);
    // A mirror maximised for the guest is maximised on the guest's own
    // screen - the primary output, here - as Windows maximises a window on
    // the monitor it is on; KWin would maximise it on the one its user is
    // on. Once per surface.
    void keep_on_guest_screen(WindowId window, std::uint32_t generation);
    void forget(WindowId window);
    // The compositor drags the mirror of ``window`` now, for a move or size
    // Windows began (MoveSizeStart). Until the drag ends the guest window is
    // not followed: pointer motion sent before the drag was handed over has
    // Windows' own move loop moving it, and placing the mirror there fights
    // the drag, then puts the mirror back where the guest got to.
    void hold_for_drag(WindowId window);
    // A press on the mirror: a drag the compositor never ran is over.
    void end_drag_hold(WindowId window);
    // The guest resized its window itself, and the mirror took the size:
    // where KWin puts a client that resizes itself (keeping its centre) is
    // not where the guest has it, and the mirror goes back to the guest's
    // corner rather than the guest window following KWin's.
    void guest_resized(WindowId window);

    // Where the guest's screen - this desktop's primary output - is, as the
    // compositor lays it out; nothing where it does not say.
    [[nodiscard]] std::optional<ScreenMapping> guest_screen() const { return kwin_.primary_output(); }

    // The mirror of a window was put somewhere here: its guest window's
    // picture is to be at this point of the guest's screen.
    Signal<WindowId, Point> moved_here;
    // The compositor minimised the mirror of a window, or brought it back.
    Signal<WindowId, bool> minimized_here;

private:
    struct Tracked {
        // The picture's corner on the guest's screen.
        Point guest;
        std::uint32_t generation = 0;
        bool placeable = false;
        // Minimised by the compositor, as it last said.
        bool minimized = false;
        // The compositor is dragging it (hold_for_drag).
        bool held = false;
        // When the guest last resized the window itself (guest_resized).
        std::int64_t guest_resized_at = 0;
        bool pending = false;
        // A placement of this window has been done, or asked for again once
        // KWin had it.
        bool placed_once = false;
        std::optional<PointF> asked;
        std::optional<BoxF> reported;
        // A guest move asked for and not yet reported back: when it is, it
        // is this side's own move coming back.
        std::optional<Point> awaiting_guest;
        // When this side's placement was done: KWin's own placement of a
        // new window - on the output its user is on - can be reported after
        // it, and is not the user moving the window.
        std::int64_t placed_at = 0;
        // Put back once already for this placement: a second report is
        // somebody's own.
        bool put_back = false;
        std::uint32_t on_guest_screen = 0;
        // Where this side has lately asked KWin to put the window, as guest
        // points, newest last. While Windows moves a window itself - a tool
        // panel's drag - placements follow it every frame, and KWin's report
        // of one can come after the guest has gone on: it is this side's own
        // placement coming back, not the user moving the window.
        std::deque<Point> recently_asked;
    };

    static constexpr std::size_t kRecentlyAsked = 32;

    // ``key``: WindowId::key, which is what KWinPlacement knows a window by.
    void placed(std::uint64_t key, PointF frame_corner);
    void not_placed(std::uint64_t key);
    void moved(std::uint64_t key, const KWinPlacement::Report& report);
    void place_at_guest(WindowId window, Tracked& tracked, bool remade);
    // A place KWin chose for a window just placed, rather than its user:
    // put back where this side placed it. True when it was.
    bool put_back(std::uint64_t key, Tracked& tracked, const KWinPlacement::Report& report, Point guest,
                  const ScreenMapping& screen, const std::optional<BoxF>& before);

    KWinPlacement kwin_;
    std::unordered_map<std::uint64_t, Tracked> tracked_;
};

}  // namespace rwin::host
