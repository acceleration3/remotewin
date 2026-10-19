#pragma once

#include "host/geometry/point_f.hpp"

#include <cstdint>
#include <functional>
#include <memory>

struct zwp_relative_pointer_manager_v1;
struct zwp_relative_pointer_v1;
struct wl_pointer;

namespace rwin::host {

// The pointer's own motion (zwp_relative_pointer_v1), told whenever the
// pointer moves over one of this client's surfaces - locked or not: in the
// device's counts before any acceleration, and as the pointer itself moved,
// in logical units.
class RelativePointer {
public:
    using Moved = std::function<void(PointF unaccelerated, PointF accelerated)>;

    RelativePointer(zwp_relative_pointer_manager_v1* manager, wl_pointer* pointer, Moved moved);

private:
    struct Release {
        void operator()(zwp_relative_pointer_v1* relative) const noexcept;
    };

    static void motion(void* self, zwp_relative_pointer_v1* relative, std::uint32_t time_high,
                       std::uint32_t time_low, std::int32_t dx, std::int32_t dy, std::int32_t dx_unaccelerated,
                       std::int32_t dy_unaccelerated);

    Moved moved_;
    std::unique_ptr<zwp_relative_pointer_v1, Release> relative_;
};

}  // namespace rwin::host
