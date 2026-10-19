#include "host/wayland/relative_pointer.hpp"

#include <relative-pointer-unstable-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

void RelativePointer::Release::operator()(zwp_relative_pointer_v1* relative) const noexcept {
    zwp_relative_pointer_v1_destroy(relative);
}

RelativePointer::RelativePointer(zwp_relative_pointer_manager_v1* manager, wl_pointer* pointer, Moved moved)
    : moved_(std::move(moved)), relative_(zwp_relative_pointer_manager_v1_get_relative_pointer(manager, pointer)) {
    static constexpr zwp_relative_pointer_v1_listener kListener{&RelativePointer::motion};
    zwp_relative_pointer_v1_add_listener(relative_.get(), &kListener, this);
}

// wl_fixed_t, in 256ths: unaccelerated motion is in the device's own units,
// whole counts for a mouse.
void RelativePointer::motion(void* self, zwp_relative_pointer_v1* /*relative*/, std::uint32_t /*time_high*/,
                             std::uint32_t /*time_low*/, std::int32_t dx, std::int32_t dy,
                             std::int32_t dx_unaccelerated, std::int32_t dy_unaccelerated) {
    auto* pointer = static_cast<RelativePointer*>(self);
    pointer->moved_(PointF{wl_fixed_to_double(dx_unaccelerated), wl_fixed_to_double(dy_unaccelerated)},
                    PointF{wl_fixed_to_double(dx), wl_fixed_to_double(dy)});
}

}  // namespace rwin::host
