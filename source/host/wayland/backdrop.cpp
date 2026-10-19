#include "host/wayland/backdrop.hpp"

#include "host/wayland/globals.hpp"

#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>

namespace rwin::host {

Backdrop::Backdrop(const Globals& globals, wl_surface* parent)
    : black_(globals.shm(), Size{1, 1}), surface_(wl_compositor_create_surface(globals.compositor())),
      subsurface_(wl_subcompositor_get_subsurface(globals.subcompositor(), surface_, parent)),
      viewport_(wp_viewporter_get_viewport(globals.viewporter(), surface_)) {
    const std::span<std::uint8_t> pixel = black_.pixels();
    std::fill(pixel.begin(), pixel.end(), std::uint8_t{0});
    pixel[3] = 0xFF;
    wl_subsurface_place_below(subsurface_, parent);
    wl_subsurface_set_desync(subsurface_);
    wl_region* nothing = wl_compositor_create_region(globals.compositor());
    wl_surface_set_input_region(surface_, nothing);
    wl_region_destroy(nothing);
}

Backdrop::~Backdrop() {
    wp_viewport_destroy(viewport_);
    wl_subsurface_destroy(subsurface_);
    wl_surface_destroy(surface_);
}

void Backdrop::cover(const Box& area) {
    if (area == area_ || area.empty()) return;
    area_ = area;
    wl_subsurface_set_position(subsurface_, area.x, area.y);
    wp_viewport_set_destination(viewport_, area.width, area.height);
    wl_surface_attach(surface_, black_.buffer(), 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, 1, 1);
    wl_surface_commit(surface_);
}

}  // namespace rwin::host
