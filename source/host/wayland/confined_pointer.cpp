#include "host/wayland/confined_pointer.hpp"

#include "logging/logger.hpp"

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

void ConfinedPointer::Release::operator()(zwp_confined_pointer_v1* confinement) const noexcept {
    zwp_confined_pointer_v1_destroy(confinement);
}

ConfinedPointer::ConfinedPointer(wl_compositor* compositor, zwp_pointer_constraints_v1* constraints,
                                 wl_surface* surface, wl_pointer* pointer, const Box& area) {
    wl_region* region = wl_compositor_create_region(compositor);
    wl_region_add(region, area.x, area.y, area.width, area.height);
    confinement_.reset(zwp_pointer_constraints_v1_confine_pointer(constraints, surface, pointer, region,
                                                                  ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT));
    wl_region_destroy(region);
    static constexpr zwp_confined_pointer_v1_listener kListener{&ConfinedPointer::confined,
                                                                &ConfinedPointer::unconfined};
    zwp_confined_pointer_v1_add_listener(confinement_.get(), &kListener, this);
}

void ConfinedPointer::confined(void* self, zwp_confined_pointer_v1* ) {
    static_cast<ConfinedPointer*>(self)->engaged_ = true;
    RWIN_INFO("pointer", "the compositor confined the pointer");
}

void ConfinedPointer::unconfined(void* self, zwp_confined_pointer_v1* ) {
    auto* confinement = static_cast<ConfinedPointer*>(self);
    confinement->engaged_ = false;
    confinement->over_ = true;
    RWIN_INFO("pointer", "the compositor let the pointer out of its confinement");
}

}
