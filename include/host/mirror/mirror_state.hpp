#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/size.hpp"
#include "host/mirror/mirror_role.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <string>

namespace rwin::host {

// What a mirror decided to be, read off it rather than recomputed: for a
// person or a tool comparing it with the window it mirrors
// (WindowReport). Nothing in the mirror reads this.
struct MirrorState {
    WindowId window;
    MirrorRole role;
    // A surface made for it, and a buffer committed to that.
    bool shown = false;
    bool mapped = false;
    // Its picture came last on the GPU (a dma-buf), not as pixels.
    bool on_gpu = false;
    // What the compositor last said of a toplevel.
    bool activated = false;
    bool maximized = false;
    bool fullscreen = false;
    // Asked to be minimised, maximised or fullscreen by this side.
    bool asked_minimized = false;
    bool asked_maximized = false;
    bool asked_fullscreen = false;
    // A modal dialog of its parent (xdg_dialog_v1).
    bool modal = false;
    // A popup that took the seat's grab.
    bool grabs = false;
    // Where the compositor put a popup, against its parent.
    Box placed;
    std::string title;
    Size logical;
    double scale = 1.0;
    Size picture;
    std::uint64_t frames = 0;
    // Toplevel surfaces made for it so far: a new one the compositor has
    // placed afresh.
    std::uint32_t generation = 0;
};

}  // namespace rwin::host
