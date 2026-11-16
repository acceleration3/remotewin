#include "host/wayland/backdrop.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/seat.hpp"
#include "host/wayland/surface_kit.hpp"

#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>

namespace rwin::host {

Backdrop::Backdrop(const SurfaceKit& kit, wl_surface* parent)
    : seat_(kit.seat), black_(kit.globals.shm(), Size{1, 1}), surface_(wl_compositor_create_surface(kit.globals.compositor())),
      subsurface_(wl_subcompositor_get_subsurface(kit.globals.subcompositor(), surface_, parent)),
      viewport_(wp_viewporter_get_viewport(kit.globals.viewporter(), surface_)) {
    const std::span<std::uint8_t> pixel = black_.pixels();
    std::fill(pixel.begin(), pixel.end(), std::uint8_t{0});
    pixel[3] = 0xFF;
    wl_subsurface_place_below(subsurface_, parent);
    wl_subsurface_set_desync(subsurface_);
}

Backdrop::~Backdrop() {
    seat_.forget(surface_);
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

}
