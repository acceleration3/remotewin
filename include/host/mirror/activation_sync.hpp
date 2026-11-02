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

class ActivationSync {
public:
    ActivationSync(EventLoop& loop, GuestLink& link, Seat& seat);

    bool focused(WindowId window, FocusCause cause);
    void follow(const ZOrder& order, WindowMirror* active);

private:
    void keyboard_moved();
    void focus_left();

    GuestLink& link_;
    Seat& seat_;
    WindowId host_active_;
    WindowId guest_active_;
    std::unique_ptr<EventLoop::Timer> settle_;
};

}
