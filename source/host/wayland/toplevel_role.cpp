#include "host/wayland/toplevel_role.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/toplevel_events.hpp"

#include <wayland-client.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-dialog-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>

#include <cstring>

namespace rwin::host {

namespace {

// Numbers the roles made, so a parent is told apart from one freed before
// it at the same address.
std::uint64_t roles_made = 0;

}  // namespace

ToplevelRole::ToplevelRole(const Globals& globals, MirrorSurface& surface, ToplevelEvents& events,
                           bool server_decorated)
    : globals_(globals), surface_(surface), events_(events),
      xdg_surface_(xdg_wm_base_get_xdg_surface(globals.wm_base(), surface.surface())),
      toplevel_(xdg_surface_get_toplevel(xdg_surface_)), serial_(++roles_made) {
    static constexpr xdg_surface_listener kSurface{&ToplevelRole::surface_configured};
    xdg_surface_add_listener(xdg_surface_, &kSurface, this);
    static constexpr xdg_toplevel_listener kToplevel{&ToplevelRole::toplevel_configured, &ToplevelRole::closed,
                                                     &ToplevelRole::bounded, &ToplevelRole::capable};
    xdg_toplevel_add_listener(toplevel_, &kToplevel, this);
    if (globals.decoration() != nullptr) {
        decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(globals.decoration(), toplevel_);
        zxdg_toplevel_decoration_v1_set_mode(decoration_, server_decorated
                                                              ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                                              : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
    }
}

ToplevelRole::~ToplevelRole() {
    if (dialog_ != nullptr) xdg_dialog_v1_destroy(dialog_);
    if (decoration_ != nullptr) zxdg_toplevel_decoration_v1_destroy(decoration_);
    xdg_toplevel_destroy(toplevel_);
    xdg_surface_destroy(xdg_surface_);
}

void ToplevelRole::surface_configured(void* self, xdg_surface* surface, std::uint32_t serial) {
    auto* role = static_cast<ToplevelRole*>(self);
    xdg_surface_ack_configure(surface, serial);
    role->last_ = role->pending_;
    const bool first = !role->configured_;
    role->configured_ = true;
    role->events_.configured(role->last_);
    if (first) role->surface_.allow_commits();
}

void ToplevelRole::toplevel_configured(void* self, xdg_toplevel* /*toplevel*/, std::int32_t width,
                                       std::int32_t height, wl_array* states) {
    auto* role = static_cast<ToplevelRole*>(self);
    ToplevelConfigure configure;
    configure.size = Size{width, height};
    const auto* state = static_cast<const std::uint32_t*>(states->data);
    for (std::size_t at = 0; at < states->size / sizeof(std::uint32_t); ++at) {
        switch (state[at]) {
            case XDG_TOPLEVEL_STATE_MAXIMIZED: configure.maximized = true; break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN: configure.fullscreen = true; break;
            case XDG_TOPLEVEL_STATE_ACTIVATED: configure.activated = true; break;
            case XDG_TOPLEVEL_STATE_RESIZING: configure.resizing = true; break;
            case XDG_TOPLEVEL_STATE_TILED_LEFT:
            case XDG_TOPLEVEL_STATE_TILED_RIGHT:
            case XDG_TOPLEVEL_STATE_TILED_TOP:
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM: configure.tiled = true; break;
            case XDG_TOPLEVEL_STATE_SUSPENDED: configure.suspended = true; break;
            default: break;
        }
    }
    role->pending_ = configure;
}

void ToplevelRole::closed(void* self, xdg_toplevel* /*toplevel*/) { static_cast<ToplevelRole*>(self)->events_.close_requested(); }

void ToplevelRole::bounded(void* /*self*/, xdg_toplevel* /*toplevel*/, std::int32_t /*width*/, std::int32_t /*height*/) {}

void ToplevelRole::capable(void* /*self*/, xdg_toplevel* /*toplevel*/, wl_array* /*capabilities*/) {}

void ToplevelRole::start() { wl_surface_commit(surface_.surface()); }

void ToplevelRole::set_title(const std::string& title) { xdg_toplevel_set_title(toplevel_, title.c_str()); }

void ToplevelRole::set_app_id(const std::string& app_id) { xdg_toplevel_set_app_id(toplevel_, app_id.c_str()); }

void ToplevelRole::set_parent(const ToplevelRole* parent) {
    const std::uint64_t serial = parent != nullptr ? parent->serial_ : 0;
    if (serial == parent_) return;
    parent_ = serial;
    xdg_toplevel_set_parent(toplevel_, parent != nullptr ? parent->toplevel_ : nullptr);
}

void ToplevelRole::limit(Size smallest, Size largest) {
    xdg_toplevel_set_min_size(toplevel_, std::max(0, smallest.width), std::max(0, smallest.height));
    xdg_toplevel_set_max_size(toplevel_, std::max(0, largest.width), std::max(0, largest.height));
}

void ToplevelRole::set_window_geometry(const Box& geometry) {
    if (geometry.empty()) return;
    xdg_surface_set_window_geometry(xdg_surface_, geometry.x, geometry.y, geometry.width, geometry.height);
}

void ToplevelRole::maximize(bool maximized) {
    if (maximized) {
        xdg_toplevel_set_maximized(toplevel_);
    } else {
        xdg_toplevel_unset_maximized(toplevel_);
    }
}

void ToplevelRole::fullscreen(bool full) {
    if (full) {
        xdg_toplevel_set_fullscreen(toplevel_, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(toplevel_);
    }
}

void ToplevelRole::minimize() { xdg_toplevel_set_minimized(toplevel_); }

void ToplevelRole::move(wl_seat* seat, std::uint32_t serial) { xdg_toplevel_move(toplevel_, seat, serial); }

void ToplevelRole::resize(wl_seat* seat, std::uint32_t serial, std::uint32_t edges) {
    xdg_toplevel_resize(toplevel_, seat, serial, edges);
}

void ToplevelRole::set_modal(bool modal) {
    if (globals_.dialog() == nullptr || modal == modal_) return;
    modal_ = modal;
    if (dialog_ == nullptr) dialog_ = xdg_wm_dialog_v1_get_xdg_dialog(globals_.dialog(), toplevel_);
    if (modal) {
        xdg_dialog_v1_set_modal(dialog_);
    } else {
        xdg_dialog_v1_unset_modal(dialog_);
    }
}

void ToplevelRole::set_icon(xdg_toplevel_icon_v1* icon) {
    if (globals_.toplevel_icon() == nullptr) return;
    xdg_toplevel_icon_manager_v1_set_icon(globals_.toplevel_icon(), toplevel_, icon);
}

}  // namespace rwin::host
