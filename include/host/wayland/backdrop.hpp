#pragma once

#include "host/geometry/box.hpp"
#include "host/wayland/shm_buffer.hpp"

struct wl_subsurface;
struct wl_surface;
struct wp_viewport;

namespace rwin::host {

class Globals;

class Backdrop {
public:
    Backdrop(const Globals& globals, wl_surface* parent);
    ~Backdrop();

    Backdrop(const Backdrop&) = delete;
    Backdrop& operator=(const Backdrop&) = delete;
    Backdrop(Backdrop&&) = delete;
    Backdrop& operator=(Backdrop&&) = delete;

    void cover(const Box& area);

private:
    ShmBuffer black_;
    wl_surface* surface_;
    wl_subsurface* subsurface_;
    wp_viewport* viewport_;
    Box area_;
};

}
