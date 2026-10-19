#pragma once

#include "host/geometry/point_f.hpp"

#include <memory>
#include <optional>

struct wl_pointer;
struct wl_surface;
struct zwp_locked_pointer_v1;
struct zwp_pointer_constraints_v1;

namespace rwin::host {

// The pointer held still on one surface (zwp_pointer_constraints_v1
// lock_pointer), for as long as this lives: what a game's mouse look asks
// for. Asked once (oneshot): when the compositor lets go of it - the window
// lost focus, its own unlock - it is over, and holding again is a new one.
//
// The compositor engages it when it chooses (the surface has the pointer
// and the focus) and says so; until then it is only asked for.
class LockedPointer {
public:
    // ``hint`` is where the pointer is to be when it is let go, in the
    // surface's logical units.
    LockedPointer(zwp_pointer_constraints_v1* constraints, wl_surface* surface, wl_pointer* pointer,
                  std::optional<PointF> hint);

    LockedPointer(const LockedPointer&) = delete;
    LockedPointer& operator=(const LockedPointer&) = delete;
    LockedPointer(LockedPointer&&) = delete;
    LockedPointer& operator=(LockedPointer&&) = delete;
    ~LockedPointer() = default;

    [[nodiscard]] bool engaged() const noexcept { return engaged_; }
    // Engaged and then let go of by the compositor.
    [[nodiscard]] bool over() const noexcept { return over_; }

private:
    // zwp_locked_pointer_v1.destroy, which its generated header defines
    // inline: kept out of this one.
    struct Release {
        void operator()(zwp_locked_pointer_v1* lock) const noexcept;
    };

    static void locked(void* self, zwp_locked_pointer_v1* lock);
    static void unlocked(void* self, zwp_locked_pointer_v1* lock);

    std::unique_ptr<zwp_locked_pointer_v1, Release> lock_;
    bool engaged_ = false;
    bool over_ = false;
};

}  // namespace rwin::host
