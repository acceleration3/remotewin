#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/box_f.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
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
class GuestLink;
class GuestWindow;

// Keeps a toplevel mirror where its guest window is, where the compositor
// can be told and asked (KWinPlacement): a window the guest moves -
// SetWindowPos, a dialog centred on its owner, CW_USEDEFAULT - is put at
// the same place on this desktop, and a mirror moved here - dragged by the
// user, placed by the compositor - has its guest window moved under it, so
// the guest's own idea of where the window is (the monitor a dialog centres
// on) is what is on screen. A nicety for first placement and for keeping
// the two in step: nothing depends on it, input reaches a window wherever
// either side has it (R2). Elsewhere the compositor places mirrors and the
// guest is not told.
class PlacementSync {
public:
    PlacementSync(EventLoop& loop, BusConnection& bus, GuestLink& link);

    [[nodiscard]] bool active() const noexcept { return kwin_.active(); }

    // What the mirror of ``window`` is known to the compositor by: a suffix
    // of its title, or nothing where nothing is placed.
    [[nodiscard]] std::string tag_for(WindowId window) const;

    // Where the guest window's picture is, as the guest last said, for a
    // toplevel mirror of ``logical`` units whose surface is the
    // ``generation``-th made for it. ``placeable`` is false for a window
    // minimised or maximised in the guest: those are placed by their state.
    // A mirror whose surface was made again is placed again: the compositor
    // put the new one where it chose.
    void follow_guest(const GuestWindow& window, std::uint32_t generation, Size logical, bool placeable);
    // A mirror maximised for the guest is maximised on the guest's own
    // screen - the primary output, here - as Windows maximises a window on
    // the monitor it is on; KWin would maximise it on the one its user is
    // on. Once per surface.
    void keep_on_guest_screen(WindowId window, std::uint32_t generation);
    void forget(WindowId window);

    // Where the guest's screen - this desktop's primary output - is, as the
    // compositor lays it out; nothing where it does not say.
    [[nodiscard]] std::optional<ScreenMapping> guest_screen() const { return kwin_.primary_output(); }

private:
    struct Tracked {
        // The picture's corner on the guest's screen; the frame's size, and
        // where the picture sits in it: what the guest is told when the
        // window is put somewhere here.
        Point guest;
        Size frame_size;
        Point picture_offset;
        std::uint32_t generation = 0;
        bool placeable = false;
        bool pending = false;
        // A placement of this window has been done, or asked for again once
        // KWin had it.
        bool placed_once = false;
        std::optional<PointF> asked;
        Size asked_size;
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
    void moved(std::uint64_t key, const BoxF& frame);

    GuestLink& link_;
    KWinPlacement kwin_;
    std::unordered_map<std::uint64_t, Tracked> tracked_;
};

}  // namespace rwin::host
