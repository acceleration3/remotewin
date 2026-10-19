#pragma once

#include "host/geometry/box.hpp"
#include "host/wayland/shm_buffer.hpp"

struct wl_subsurface;
struct wl_surface;
struct wp_viewport;

namespace rwin::host {

class Globals;

// Black bars around a picture that does not fill its window: a maximised
// or fullscreen mirror whose guest window is not the size the compositor
// gave it is shown one to one, centred, over this - a subsurface below the
// picture's surface, one black pixel stretched over the window - rather
// than stretched. It takes no input.
class Backdrop {
public:
    Backdrop(const Globals& globals, wl_surface* parent);
    ~Backdrop();

    Backdrop(const Backdrop&) = delete;
    Backdrop& operator=(const Backdrop&) = delete;
    Backdrop(Backdrop&&) = delete;
    Backdrop& operator=(Backdrop&&) = delete;

    // Over ``area`` of the parent, in its logical units; applied with the
    // parent's next commit.
    void cover(const Box& area);

private:
    ShmBuffer black_;
    wl_surface* surface_;
    wl_subsurface* subsurface_;
    wp_viewport* viewport_;
    Box area_;
};

}  // namespace rwin::host
