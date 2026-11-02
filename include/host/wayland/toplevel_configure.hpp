#pragma once

#include "host/geometry/size.hpp"

namespace rwin::host {
struct ToplevelConfigure {
    Size size;
    bool maximized = false;
    bool fullscreen = false;
    bool activated = false;
    bool resizing = false;
    bool tiled = false;
    bool suspended = false;

    [[nodiscard]] bool operator==(const ToplevelConfigure&) const noexcept = default;
};

}
