#pragma once

#include "host/geometry/point_f.hpp"

#include <cstdint>

namespace rwin::host {

struct KeyPress;

// What a surface of this client is told of the seat's pointer and
// keyboard (MirrorSurface::input), once each pointer frame is whole
// (wl_pointer.frame): a mirror
// of a guest window, or of the guest's monitor. Points are in the
// surface's logical units.
class SurfaceInput {
public:
    // A wheel or touchpad scroll: 120ths of a notch, as Windows' WHEEL_DELTA
    // counts them; positive is down and right, as Wayland has it.
    struct Wheel {
        std::int32_t dx120 = 0;
        std::int32_t dy120 = 0;
    };

    virtual void pointer_entered(PointF at) = 0;
    virtual void pointer_moved(PointF at) = 0;
    virtual void pointer_left() = 0;
    // A linux/input-event-codes.h button (BTN_LEFT...).
    virtual void button(std::uint32_t code, bool pressed) = 0;
    virtual void wheel(Wheel turned) = 0;
    virtual void keyboard_entered() = 0;
    virtual void keyboard_left() = 0;
    virtual void key(const KeyPress& press) = 0;

protected:
    ~SurfaceInput() = default;
};

}  // namespace rwin::host
