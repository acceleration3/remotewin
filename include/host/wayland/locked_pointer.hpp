#pragma once

#include "host/geometry/point_f.hpp"

#include <memory>
#include <optional>

struct wl_pointer;
struct wl_surface;
struct zwp_locked_pointer_v1;
struct zwp_pointer_constraints_v1;

namespace rwin::host {
class LockedPointer {
public:
    LockedPointer(zwp_pointer_constraints_v1* constraints, wl_surface* surface, wl_pointer* pointer,
                  std::optional<PointF> hint);

    LockedPointer(const LockedPointer&) = delete;
    LockedPointer& operator=(const LockedPointer&) = delete;
    LockedPointer(LockedPointer&&) = delete;
    LockedPointer& operator=(LockedPointer&&) = delete;
    ~LockedPointer() = default;

    [[nodiscard]] bool engaged() const noexcept { return engaged_; }
    [[nodiscard]] bool over() const noexcept { return over_; }

private:
    struct Release {
        void operator()(zwp_locked_pointer_v1* lock) const noexcept;
    };

    static void locked(void* self, zwp_locked_pointer_v1* lock);
    static void unlocked(void* self, zwp_locked_pointer_v1* lock);

    std::unique_ptr<zwp_locked_pointer_v1, Release> lock_;
    bool engaged_ = false;
    bool over_ = false;
};

}
