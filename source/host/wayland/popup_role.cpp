#include "host/wayland/popup_role.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/popup_events.hpp"

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>

#include <algorithm>

namespace rwin::host {

namespace {

constexpr std::uint32_t kRepositionVersion = 3;

}  // namespace

PopupRole::PopupRole(const Globals& globals, MirrorSurface& surface, xdg_surface* parent,
                     const PopupPlacement& placement, wl_seat* seat, std::uint32_t grab_serial, PopupEvents& events)
    : globals_(globals), surface_(surface), events_(events), placement_(placement),
      xdg_surface_(xdg_wm_base_get_xdg_surface(globals.wm_base(), surface.surface())) {
    static constexpr xdg_surface_listener kSurface{&PopupRole::surface_configured};
    xdg_surface_add_listener(xdg_surface_, &kSurface, this);
    xdg_positioner* positioner = positioner_for(placement);
    popup_ = xdg_surface_get_popup(xdg_surface_, parent, positioner);
    xdg_positioner_destroy(positioner);
    static constexpr xdg_popup_listener kPopup{&PopupRole::popup_configured, &PopupRole::done,
                                               &PopupRole::repositioned};
    xdg_popup_add_listener(popup_, &kPopup, this);
    if (!placement.size.empty()) {
        xdg_surface_set_window_geometry(xdg_surface_, 0, 0, placement.size.width, placement.size.height);
    }
    if (grab_serial != 0) {
        xdg_popup_grab(popup_, seat, grab_serial);
        grabs_ = true;
    }
    wl_surface_commit(surface.surface());
}

PopupRole::~PopupRole() {
    xdg_popup_destroy(popup_);
    xdg_surface_destroy(xdg_surface_);
}

xdg_positioner* PopupRole::positioner_for(const PopupPlacement& placement) const {
    xdg_positioner* positioner = xdg_wm_base_create_positioner(globals_.wm_base());
    xdg_positioner_set_size(positioner, std::max(1, placement.size.width), std::max(1, placement.size.height));
    xdg_positioner_set_anchor_rect(positioner, placement.anchor.x, placement.anchor.y,
                                   std::max(0, placement.anchor.width), std::max(0, placement.anchor.height));
    xdg_positioner_set_anchor(positioner, placement.edge);
    xdg_positioner_set_gravity(positioner, placement.gravity);
    xdg_positioner_set_constraint_adjustment(positioner, placement.adjust);
    xdg_positioner_set_offset(positioner, placement.offset.x, placement.offset.y);
    return positioner;
}

bool PopupRole::repositions() const noexcept { return globals_.wm_base_version() >= kRepositionVersion; }

void PopupRole::reposition(const PopupPlacement& placement) {
    if (placement == placement_) return;
    placement_ = placement;
    xdg_positioner* positioner = positioner_for(placement);
    xdg_popup_reposition(popup_, positioner, ++reposition_token_);
    xdg_positioner_destroy(positioner);
    if (!placement.size.empty()) {
        xdg_surface_set_window_geometry(xdg_surface_, 0, 0, placement.size.width, placement.size.height);
    }
}

void PopupRole::surface_configured(void* self, xdg_surface* surface, std::uint32_t serial) {
    auto* role = static_cast<PopupRole*>(self);
    xdg_surface_ack_configure(surface, serial);
    const bool first = !role->configured_;
    role->configured_ = true;
    role->events_.popup_placed(role->placed_);
    if (first) role->surface_.allow_commits();
}

void PopupRole::popup_configured(void* self, xdg_popup* /*popup*/, std::int32_t x, std::int32_t y,
                                 std::int32_t width, std::int32_t height) {
    static_cast<PopupRole*>(self)->placed_ = Box{x, y, width, height};
}

void PopupRole::done(void* self, xdg_popup* /*popup*/) { static_cast<PopupRole*>(self)->events_.popup_dismissed(); }

void PopupRole::repositioned(void* /*self*/, xdg_popup* /*popup*/, std::uint32_t /*token*/) {}

}  // namespace rwin::host
