#include "host/wayland/locked_pointer.hpp"

#include "logging/logger.hpp"

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

void LockedPointer::Release::operator()(zwp_locked_pointer_v1* lock) const noexcept {
    zwp_locked_pointer_v1_destroy(lock);
}

LockedPointer::LockedPointer(zwp_pointer_constraints_v1* constraints, wl_surface* surface, wl_pointer* pointer,
                             std::optional<PointF> hint)
    : lock_(zwp_pointer_constraints_v1_lock_pointer(constraints, surface, pointer, nullptr,
                                                    ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT)) {
    static constexpr zwp_locked_pointer_v1_listener kListener{&LockedPointer::locked, &LockedPointer::unlocked};
    zwp_locked_pointer_v1_add_listener(lock_.get(), &kListener, this);
    if (hint.has_value()) {
        zwp_locked_pointer_v1_set_cursor_position_hint(lock_.get(), wl_fixed_from_double(hint->x),
                                                       wl_fixed_from_double(hint->y));
    }
}

void LockedPointer::locked(void* self, zwp_locked_pointer_v1* ) {
    static_cast<LockedPointer*>(self)->engaged_ = true;
    RWIN_INFO("pointer", "the compositor locked the pointer");
}

void LockedPointer::unlocked(void* self, zwp_locked_pointer_v1* ) {
    auto* lock = static_cast<LockedPointer*>(self);
    lock->engaged_ = false;
    lock->over_ = true;
    RWIN_INFO("pointer", "the compositor unlocked the pointer");
}

}
