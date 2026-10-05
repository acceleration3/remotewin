#pragma once

#include <QRegion>

#include <memory>

struct wl_pointer;
struct wl_surface;
struct zwp_confined_pointer_v1;
struct zwp_pointer_constraints_v1;

namespace rwin::host {

// The pointer kept within a region of one surface
// (zwp_pointer_constraints_v1 confine_pointer), for as long as this lives:
// ClipCursor, on this side. Asked once (oneshot), like LockedPointer.
class ConfinedPointer {
public:
    // ``region`` in the surface's logical units; empty is the whole surface.
    ConfinedPointer(zwp_pointer_constraints_v1* constraints, wl_surface* surface, wl_pointer* pointer,
                    const QRegion& region);

    ConfinedPointer(const ConfinedPointer&) = delete;
    ConfinedPointer& operator=(const ConfinedPointer&) = delete;
    ConfinedPointer(ConfinedPointer&&) = delete;
    ConfinedPointer& operator=(ConfinedPointer&&) = delete;
    ~ConfinedPointer() = default;

    [[nodiscard]] bool engaged() const noexcept { return engaged_; }
    [[nodiscard]] bool over() const noexcept { return over_; }

private:
    struct Release {
        void operator()(zwp_confined_pointer_v1* confinement) const noexcept;
    };

    static void confined(void* self, zwp_confined_pointer_v1* confinement);
    static void unconfined(void* self, zwp_confined_pointer_v1* confinement);

    std::unique_ptr<zwp_confined_pointer_v1, Release> confinement_;
    bool engaged_ = false;
    bool over_ = false;
};

}  // namespace rwin::host
