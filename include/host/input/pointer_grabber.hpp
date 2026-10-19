#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/wayland/confined_pointer.hpp"
#include "host/wayland/locked_pointer.hpp"
#include "host/wayland/relative_pointer.hpp"
#include "host/wayland/shortcuts_inhibitor.hpp"
#include "proto/pointer_grab.hpp"

#include <cstdint>
#include <memory>

namespace rwin::host {

class Globals;
class GuestLink;
class Seat;
class WindowMirror;
struct KeyPress;

// Holds this desktop's pointer the way the guest application holds its
// own (PointerGrab): confined to the part of the mirror that is the guest's
// clip rectangle, or locked on the mirror with only its motion passed on,
// as counts the guest moves its cursor by - which is what reaches a game's
// raw input. While it is locked, the compositor's own shortcuts are held
// back from the mirror, as they are from a game on Windows.
//
// Asked is not held: the compositor holds the pointer when it chooses (the
// mirror has the focus and the pointer is over it) and says so. Until then
// the pointer moves freely over the mirror, as positions, and its motion is
// not passed on.
//
// Let go of when the guest stops asking, when the mirror loses the focus
// (and held again when it gets it back), and when the user presses the
// escape, Ctrl+Alt+G - QEMU's - after which a click on the mirror holds it
// again for as long as the guest still asks.
class PointerGrabber {
public:
    PointerGrabber(GuestLink& link, const Globals& globals, Seat& seat);
    ~PointerGrabber();

    PointerGrabber(const PointerGrabber&) = delete;
    PointerGrabber& operator=(const PointerGrabber&) = delete;
    PointerGrabber(PointerGrabber&&) = delete;
    PointerGrabber& operator=(PointerGrabber&&) = delete;

    // What the guest wants now; ``mirror`` is the mirror of the window it
    // is for, or null, and ``guest_origin`` where that window's picture is
    // on the guest's screen.
    void follow(const PointerGrab& grab, WindowMirror* mirror, Point guest_origin);
    // A mirror is going, or its surface is: whatever it held is let go.
    void forget(const WindowMirror& mirror);
    // The keyboard moved: a mirror that lost it lets go, one that has it
    // back holds again.
    void keyboard_moved();

    // The escape, pressed in a mirror while something is held or wanted:
    // taken here, and not passed on.
    [[nodiscard]] bool intercept(const KeyPress& press);
    // A press in ``mirror`` after the escape: it holds again, and the press
    // is not passed on.
    [[nodiscard]] bool intercept_press(const WindowMirror& mirror);
    // The pointer's own motion is all that reaches ``mirror``'s window.
    [[nodiscard]] bool locked_on(const WindowMirror& mirror) const;

private:
    [[nodiscard]] bool wanted() const noexcept;
    void hold();
    void let_go();
    void notice_compositor_let_go();
    void moved(PointF unaccelerated);

    GuestLink& link_;
    const Globals& globals_;
    Seat& seat_;
    PointerGrab grab_;
    WindowMirror* mirror_ = nullptr;
    Point origin_;
    std::unique_ptr<LockedPointer> lock_;
    std::unique_ptr<ConfinedPointer> confinement_;
    std::unique_ptr<RelativePointer> relative_;
    std::unique_ptr<ShortcutsInhibitor> inhibitor_;
    bool escaped_ = false;
    bool swallow_escape_release_ = false;
    // Motion under a whole count, carried to the next.
    double owed_x_ = 0.0;
    double owed_y_ = 0.0;
};

}  // namespace rwin::host
