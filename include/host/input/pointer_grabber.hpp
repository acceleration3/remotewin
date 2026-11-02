#pragma once

#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/wayland/confined_pointer.hpp"
#include "host/wayland/locked_pointer.hpp"
#include "host/wayland/relative_pointer.hpp"
#include "proto/pointer_grab.hpp"

#include <cstdint>
#include <memory>

namespace rwin::host {

class Globals;
class GuestLink;
class Seat;
class WindowMirror;
struct KeyPress;

class PointerGrabber {
public:
    PointerGrabber(GuestLink& link, const Globals& globals, Seat& seat);
    ~PointerGrabber();

    PointerGrabber(const PointerGrabber&) = delete;
    PointerGrabber& operator=(const PointerGrabber&) = delete;
    PointerGrabber(PointerGrabber&&) = delete;
    PointerGrabber& operator=(PointerGrabber&&) = delete;

    void follow(const PointerGrab& grab, WindowMirror* mirror, Point guest_origin);
    void forget(const WindowMirror& mirror);
    void keyboard_moved();

    [[nodiscard]] bool intercept(const KeyPress& press);
    [[nodiscard]] bool intercept_press(const WindowMirror& mirror);
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
    bool escaped_ = false;
    bool swallow_escape_release_ = false;
    double owed_x_ = 0.0;
    double owed_y_ = 0.0;
};

}
