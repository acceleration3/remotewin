#pragma once

#include "host/loop/event_loop.hpp"
#include "host/mirror/focus_cause.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <memory>

namespace rwin::host {

class GuestLink;
class Seat;
class WindowMirror;

// Keeps the guest's active window and this desktop's focus in step, by
// the rule a RAIL client keeps (docs/rail-model.md §2.6, §5.5):
//
// - A press in a mirror activates its window in the guest by itself, as
//   input; nothing is said. Nor is the compositor focusing a mirror as it
//   maps it while the guest has another window active: a RAIL client makes
//   its windows without activating them, and Windows decided which is. Focus that comes any other way - Alt+Tab, the
//   taskbar, the compositor focusing a window it mapped - is said
//   (Activate), unless the guest has that window active already.
// - Focus leaving every mirror for something that is not one is said too,
//   and the guest makes its marker window active: nothing it shows is.
// - The guest's own activations are followed only while the user is in
//   one of these mirrors, and only for a window above the marker: one made
//   while the user was elsewhere must not take the focus from there.
class ActivationSync {
public:
    ActivationSync(EventLoop& loop, GuestLink& link, Seat& seat);

    // This desktop focused the mirror of ``window``. False when the guest
    // is not told: a press told it already, it has the window active, or the
    // compositor only focused what it mapped while the guest has another
    // window active - which is then the one to follow (follow).
    bool focused(WindowId window, FocusCause cause);
    // The guest's stacking and active window; ``active`` is the mirror of
    // that window, or null when it has none here.
    void follow(const ZOrder& order, WindowMirror* active);

private:
    void keyboard_moved();
    void focus_left();

    GuestLink& link_;
    Seat& seat_;
    // The window this desktop last focused a mirror of, until focus leaves
    // them all and the guest is told.
    WindowId host_active_;
    WindowId guest_active_;
    // Focus going from one mirror to another passes through none of them
    // for a moment; only focus still gone after it has left them all.
    std::unique_ptr<EventLoop::Timer> settle_;
};

}  // namespace rwin::host
