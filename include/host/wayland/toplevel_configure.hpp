#pragma once

#include "host/geometry/size.hpp"

namespace rwin::host {

// What the compositor asked a toplevel to be (xdg_toplevel.configure): a
// size in logical units - empty for "as the client likes" - and its states.
struct ToplevelConfigure {
    Size size;
    bool maximized = false;
    bool fullscreen = false;
    bool activated = false;
    bool resizing = false;
    bool tiled = false;
    // Hidden from the user entirely: minimised, on another desktop.
    bool suspended = false;

    [[nodiscard]] bool operator==(const ToplevelConfigure&) const noexcept = default;
};

}  // namespace rwin::host
