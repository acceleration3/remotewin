#include "host/wayland/confined_pointer.hpp"

#include "host/wayland/owned_proxy.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "logging/logger.hpp"

#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

void ConfinedPointer::Release::operator()(zwp_confined_pointer_v1* confinement) const noexcept {
    zwp_confined_pointer_v1_destroy(confinement);
}

// The region is copied by the request, so it is gone again at once.
ConfinedPointer::ConfinedPointer(zwp_pointer_constraints_v1* constraints, wl_surface* surface, wl_pointer* pointer,
                                 const QRegion& region) {
    OwnedProxy<wl_region, &wl_region_destroy> area;
    wl_compositor* compositor = PointerGlobals::compositor();
    if (!region.isEmpty() && compositor != nullptr) {
        area.reset(wl_compositor_create_region(compositor));
        for (const QRect& rect : region) wl_region_add(area.get(), rect.x(), rect.y(), rect.width(), rect.height());
    }
    confinement_.reset(zwp_pointer_constraints_v1_confine_pointer(constraints, surface, pointer, area.get(),
                                                                  ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT));
    static constexpr zwp_confined_pointer_v1_listener kListener{&ConfinedPointer::confined,
                                                                &ConfinedPointer::unconfined};
    zwp_confined_pointer_v1_add_listener(confinement_.get(), &kListener, this);
}

void ConfinedPointer::confined(void* self, zwp_confined_pointer_v1* /*confinement*/) {
    static_cast<ConfinedPointer*>(self)->engaged_ = true;
    RWIN_INFO("pointer", "the compositor confined the pointer");
}

void ConfinedPointer::unconfined(void* self, zwp_confined_pointer_v1* /*confinement*/) {
    auto* confinement = static_cast<ConfinedPointer*>(self);
    confinement->engaged_ = false;
    confinement->over_ = true;
    RWIN_INFO("pointer", "the compositor let the pointer out of its confinement");
}

}  // namespace rwin::host
