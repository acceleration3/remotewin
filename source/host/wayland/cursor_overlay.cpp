#include "host/wayland/cursor_overlay.hpp"

#include "host/wayland/globals.hpp"

#include <wayland-client.h>

#include <cmath>

namespace rwin::host {

CursorOverlay::CursorOverlay(const Globals& globals, wl_surface* parent)
    : surface_(globals), subsurface_(wl_subcompositor_get_subsurface(globals.subcompositor(), surface_.surface(), parent)) {
    wl_subsurface_set_desync(subsurface_);
    wl_subsurface_place_above(subsurface_, parent);
    wl_region* none = wl_compositor_create_region(globals.compositor());
    wl_surface_set_input_region(surface_.surface(), none);
    wl_region_destroy(none);
}

CursorOverlay::~CursorOverlay() { wl_subsurface_destroy(subsurface_); }

bool CursorOverlay::show(const CursorLook& look, std::size_t step, double scale, PointF at) {
    if (look.frames.empty()) {
        hide();
        return false;
    }
    const std::size_t frame = step % look.frames.size();
    if (!painted_ || id_ != look.id || step_ != frame || scale_ != scale) {
        const std::optional<Point> hotspot = surface_.paint(look.frames[frame], scale);
        if (!hotspot.has_value()) return false;
        painted_ = true;
        id_ = look.id;
        step_ = frame;
        scale_ = scale;
        hotspot_ = *hotspot;
    }
    const Point corner{static_cast<int>(std::lround(at.x)) - hotspot_.x, static_cast<int>(std::lround(at.y)) - hotspot_.y};
    if (positioned_ && corner == placed_) return false;
    positioned_ = true;
    placed_ = corner;
    wl_subsurface_set_position(subsurface_, corner.x, corner.y);
    return true;
}

void CursorOverlay::hide() {
    if (!painted_) return;
    painted_ = false;
    positioned_ = false;
    surface_.clear();
}

}
