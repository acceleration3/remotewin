#pragma once

#include "host/geometry/point_f.hpp"

#include <cstdint>

namespace rwin::host {

struct KeyPress;

class SurfaceInput {
public:
    struct Wheel {
        std::int32_t dx120 = 0;
        std::int32_t dy120 = 0;
    };

    virtual void pointer_entered(PointF at) = 0;
    virtual void pointer_moved(PointF at) = 0;
    virtual void pointer_left() = 0;
    virtual void button(std::uint32_t code, bool pressed) = 0;
    virtual void wheel(Wheel turned) = 0;
    virtual void keyboard_entered() = 0;
    virtual void keyboard_left() = 0;
    virtual void key(const KeyPress& press) = 0;

protected:
    ~SurfaceInput() = default;
};

}
