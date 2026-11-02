#include "host/mirror/activation_sync.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/window_mirror.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"

namespace rwin::host {

namespace {
constexpr std::chrono::milliseconds kFocusSettle{100};

}

ActivationSync::ActivationSync(EventLoop& loop, GuestLink& link, Seat& seat)
    : link_(link), seat_(seat), settle_(loop.timer([this] { focus_left(); })) {
    seat_.keyboard_focus_changed.connect([this] { keyboard_moved(); });
}

bool ActivationSync::focused(WindowId window, FocusCause cause) {
    settle_->stop();
    host_active_ = window;
    if (cause == FocusCause::Press || window == guest_active_) return false;
    if (cause == FocusCause::Mapping && !guest_active_.none()) {
        RWIN_DEBUG("focus", "{} focused by the compositor as it mapped; the guest has {} active", window,
                   guest_active_);
        return false;
    }
    RWIN_DEBUG("focus", "{} focused here without a press in it; activating it in the guest", window);
    link_.send_activate(window, true);
    return true;
}

void ActivationSync::follow(const ZOrder& order, WindowMirror* active) {
    guest_active_ = order.active;
    if (active == nullptr || !order.above_marker(order.active)) return;
    if (seat_.keyboard_focus() == nullptr) {
        RWIN_DEBUG("focus", "the guest activated {} while the user is elsewhere; not followed", order.active);
        return;
    }
    if (active->window() == host_active_) return;
    active->activate(std::string());
}

void ActivationSync::keyboard_moved() {
    if (seat_.keyboard_focus() != nullptr) {
        settle_->stop();
    } else if (!host_active_.none()) {
        settle_->start(kFocusSettle, false);
    }
}

void ActivationSync::focus_left() {
    if (seat_.keyboard_focus() != nullptr || host_active_.none()) return;
    RWIN_DEBUG("focus", "focus left every mirror from {}; deactivating it in the guest", host_active_);
    link_.send_activate(host_active_, false);
    host_active_ = WindowId{};
}

}
