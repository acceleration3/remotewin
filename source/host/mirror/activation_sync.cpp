#include "host/mirror/activation_sync.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"
#include "logging/logger.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QTimer>

namespace rwin::host {

namespace {

// Long enough for the compositor to have said where the focus went after
// it left a surface: a leave and the next enter come within a frame or two.
constexpr int kFocusSettleMs = 100;

}  // namespace

ActivationSync::ActivationSync(GuestLink& link, QObject* parent)
    : QObject(parent), link_(link), settle_(new QTimer(this)) {
    settle_->setSingleShot(true);
    settle_->setInterval(kFocusSettleMs);
    connect(settle_, &QTimer::timeout, this, &ActivationSync::focus_left);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, &ActivationSync::application_state_changed);
}

void ActivationSync::focused(WindowId window, bool by_press) {
    settle_->stop();
    host_active_ = window;
    if (by_press || window == guest_active_) return;
    RWIN_DEBUG("focus", "{} focused here without a press in it; activating it in the guest", window);
    link_.send_activate(window, true);
}

void ActivationSync::follow(const ZOrder& order, MirrorWindow* active) {
    guest_active_ = order.active;
    if (active == nullptr || !order.above_marker(order.active)) return;
    if (qobject_cast<MirrorWindow*>(QApplication::activeWindow()) == nullptr) {
        RWIN_DEBUG("focus", "the guest activated {} while the user is elsewhere; not followed", order.active);
        return;
    }
    active->activate_for_guest();
}

void ActivationSync::application_state_changed(Qt::ApplicationState state) {
    if (state == Qt::ApplicationActive) {
        settle_->stop();
    } else if (!host_active_.none()) {
        settle_->start();
    }
}

void ActivationSync::focus_left() {
    if (QGuiApplication::applicationState() == Qt::ApplicationActive || host_active_.none()) return;
    RWIN_DEBUG("focus", "focus left every mirror from {}; deactivating it in the guest", host_active_);
    link_.send_activate(host_active_, false);
    host_active_ = WindowId{};
}

}  // namespace rwin::host
