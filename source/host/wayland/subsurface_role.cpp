#include "host/wayland/subsurface_role.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/mirror_surface.hpp"

#include <wayland-client.h>

namespace rwin::host {

SubsurfaceRole::SubsurfaceRole(const Globals& globals, MirrorSurface& surface, wl_surface* parent)
    : subsurface_(wl_subcompositor_get_subsurface(globals.subcompositor(), surface.surface(), parent)),
      parent_(parent) {
    wl_subsurface_set_desync(subsurface_);
    wl_subsurface_place_above(subsurface_, parent);
    surface.allow_commits();
}

SubsurfaceRole::~SubsurfaceRole() { wl_subsurface_destroy(subsurface_); }

bool SubsurfaceRole::place(Point at) {
    if (placed_ && at == at_) return false;
    placed_ = true;
    at_ = at;
    wl_subsurface_set_position(subsurface_, at.x, at.y);
    return true;
}

void SubsurfaceRole::below_parent() { wl_subsurface_place_below(subsurface_, parent_); }

}  // namespace rwin::host
