#include "host/wayland/globals.hpp"

#include "logging/logger.hpp"

#include <alpha-modifier-v1-client-protocol.h>
#include <cursor-shape-v1-client-protocol.h>
#include <fractional-scale-v1-client-protocol.h>
#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#include <linux-dmabuf-v1-client-protocol.h>
#include <linux-drm-syncobj-v1-client-protocol.h>
#include <pointer-constraints-unstable-v1-client-protocol.h>
#include <presentation-time-client-protocol.h>
#include <relative-pointer-unstable-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <wayland-client.h>
#include <xdg-activation-v1-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-dialog-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-toplevel-icon-v1-client-protocol.h>

#include <algorithm>
#include <ctime>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace rwin::host {

namespace {

// The newest version of each this client speaks; a compositor's newer ones
// are not asked for.
constexpr std::uint32_t kCompositorVersion = 6;
constexpr std::uint32_t kLeastCompositorVersion = 4;
constexpr std::uint32_t kWmBaseVersion = 6;
constexpr std::uint32_t kSeatVersion = 9;
constexpr std::uint32_t kShmVersion = 1;
constexpr std::uint32_t kDmabufVersion = 5;
constexpr std::uint32_t kOutputVersion = 2;
constexpr std::uint32_t kCursorShapeVersion = 1;

void pong(void* /*self*/, xdg_wm_base* base, std::uint32_t serial) { xdg_wm_base_pong(base, serial); }

template <typename Proxy>
void destroy(Proxy* proxy) {
    if (proxy != nullptr) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(proxy));
}

template <typename Proxy>
Proxy* bound(wl_registry* registry, std::uint32_t name, const wl_interface* interface, std::uint32_t offered,
             std::uint32_t wanted) {
    return static_cast<Proxy*>(wl_registry_bind(registry, name, interface, std::min(offered, wanted)));
}

void output_geometry(void*, wl_output*, std::int32_t, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                     const char*, const char*, std::int32_t) {}
void output_mode(void*, wl_output*, std::uint32_t, std::int32_t, std::int32_t, std::int32_t) {}
void output_done(void*, wl_output*) {}

}  // namespace

Globals::Globals(wl_display* display) : display_(display) {
    registry_ = wl_display_get_registry(display_);
    static constexpr wl_registry_listener kListener{&Globals::announced, &Globals::withdrawn};
    wl_registry_add_listener(registry_, &kListener, this);
    // The globals, then what the outputs say of themselves.
    if (wl_display_roundtrip(display_) < 0 || wl_display_roundtrip(display_) < 0) {
        throw std::runtime_error("the compositor's globals could not be listed");
    }
    const char* missing = compositor_ == nullptr        ? "wl_compositor 4"
                          : subcompositor_ == nullptr   ? "wl_subcompositor"
                          : shm_ == nullptr             ? "wl_shm"
                          : wm_base_ == nullptr         ? "xdg_wm_base"
                          : seat_name_ == 0             ? "wl_seat"
                          : viewporter_ == nullptr      ? "wp_viewporter"
                                                        : nullptr;
    if (missing != nullptr) throw std::runtime_error(std::string("the compositor has no ") + missing);
    RWIN_INFO("wayland", "compositor offers {}", describe());
}

Globals::~Globals() {
    for (const Output& output : outputs_) destroy(output.output);
    destroy(dialog_);
    destroy(alpha_modifier_);
    destroy(presentation_);
    destroy(toplevel_icon_);
    destroy(decoration_);
    destroy(activation_);
    destroy(shortcuts_inhibit_);
    destroy(relative_pointer_);
    destroy(pointer_constraints_);
    destroy(cursor_shape_);
    destroy(syncobj_);
    destroy(dmabuf_);
    destroy(fractional_scale_);
    destroy(viewporter_);
    destroy(wm_base_);
    destroy(shm_);
    destroy(subcompositor_);
    destroy(compositor_);
    destroy(registry_);
}

void Globals::announced(void* self, wl_registry* /*registry*/, std::uint32_t name, const char* interface,
                        std::uint32_t version) {
    static_cast<Globals*>(self)->bind(name, interface, version);
}

void Globals::bind(std::uint32_t name, const char* interface, std::uint32_t version) {
    const std::string_view kind{interface};
    wl_registry* r = registry_;
    if (kind == wl_compositor_interface.name && version >= kLeastCompositorVersion) {
        compositor_version_ = std::min(version, kCompositorVersion);
        compositor_ = bound<wl_compositor>(r, name, &wl_compositor_interface, version, kCompositorVersion);
    } else if (kind == wl_subcompositor_interface.name) {
        subcompositor_ = bound<wl_subcompositor>(r, name, &wl_subcompositor_interface, version, 1);
    } else if (kind == wl_shm_interface.name) {
        shm_ = bound<wl_shm>(r, name, &wl_shm_interface, version, kShmVersion);
    } else if (kind == xdg_wm_base_interface.name) {
        wm_base_version_ = std::min(version, kWmBaseVersion);
        wm_base_ = bound<xdg_wm_base>(r, name, &xdg_wm_base_interface, version, kWmBaseVersion);
        static constexpr xdg_wm_base_listener kBase{&pong};
        xdg_wm_base_add_listener(wm_base_, &kBase, this);
    } else if (kind == wl_seat_interface.name && seat_name_ == 0) {
        seat_name_ = name;
        seat_version_ = std::min(version, kSeatVersion);
    } else if (kind == wp_viewporter_interface.name) {
        viewporter_ = bound<wp_viewporter>(r, name, &wp_viewporter_interface, version, 1);
    } else if (kind == wp_fractional_scale_manager_v1_interface.name) {
        fractional_scale_ = bound<wp_fractional_scale_manager_v1>(r, name, &wp_fractional_scale_manager_v1_interface,
                                                                  version, 1);
    } else if (kind == zwp_linux_dmabuf_v1_interface.name) {
        dmabuf_version_ = std::min(version, kDmabufVersion);
        dmabuf_ = bound<zwp_linux_dmabuf_v1>(r, name, &zwp_linux_dmabuf_v1_interface, version, kDmabufVersion);
    } else if (kind == wp_linux_drm_syncobj_manager_v1_interface.name) {
        syncobj_ = bound<wp_linux_drm_syncobj_manager_v1>(r, name, &wp_linux_drm_syncobj_manager_v1_interface,
                                                          version, 1);
    } else if (kind == wp_cursor_shape_manager_v1_interface.name) {
        cursor_shape_ = bound<wp_cursor_shape_manager_v1>(r, name, &wp_cursor_shape_manager_v1_interface, version,
                                                          kCursorShapeVersion);
    } else if (kind == zwp_pointer_constraints_v1_interface.name) {
        pointer_constraints_ =
            bound<zwp_pointer_constraints_v1>(r, name, &zwp_pointer_constraints_v1_interface, version, 1);
    } else if (kind == zwp_relative_pointer_manager_v1_interface.name) {
        relative_pointer_ =
            bound<zwp_relative_pointer_manager_v1>(r, name, &zwp_relative_pointer_manager_v1_interface, version, 1);
    } else if (kind == zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name) {
        shortcuts_inhibit_ = bound<zwp_keyboard_shortcuts_inhibit_manager_v1>(
            r, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, version, 1);
    } else if (kind == xdg_activation_v1_interface.name) {
        activation_ = bound<xdg_activation_v1>(r, name, &xdg_activation_v1_interface, version, 1);
    } else if (kind == zxdg_decoration_manager_v1_interface.name) {
        decoration_ = bound<zxdg_decoration_manager_v1>(r, name, &zxdg_decoration_manager_v1_interface, version, 1);
    } else if (kind == xdg_toplevel_icon_manager_v1_interface.name) {
        toplevel_icon_ =
            bound<xdg_toplevel_icon_manager_v1>(r, name, &xdg_toplevel_icon_manager_v1_interface, version, 1);
    } else if (kind == wp_presentation_interface.name) {
        presentation_ = bound<wp_presentation>(r, name, &wp_presentation_interface, version, 1);
        static constexpr wp_presentation_listener kClock{&Globals::presentation_clock};
        wp_presentation_add_listener(presentation_, &kClock, this);
    } else if (kind == wp_alpha_modifier_v1_interface.name) {
        alpha_modifier_ = bound<wp_alpha_modifier_v1>(r, name, &wp_alpha_modifier_v1_interface, version, 1);
    } else if (kind == xdg_wm_dialog_v1_interface.name) {
        dialog_ = bound<xdg_wm_dialog_v1>(r, name, &xdg_wm_dialog_v1_interface, version, 1);
    } else if (kind == wl_output_interface.name && version >= kOutputVersion) {
        auto* output = bound<wl_output>(r, name, &wl_output_interface, version, kOutputVersion);
        outputs_.push_back(Output{name, output, 1});
        static constexpr wl_output_listener kOutput{&output_geometry, &output_mode, &output_done,
                                                    &Globals::output_scaled};
        wl_output_add_listener(output, &kOutput, this);
    }
}

wl_seat* Globals::bind_seat() const {
    return static_cast<wl_seat*>(wl_registry_bind(registry_, seat_name_, &wl_seat_interface, seat_version_));
}

void Globals::withdrawn(void* self, wl_registry* /*registry*/, std::uint32_t name) {
    auto* globals = static_cast<Globals*>(self);
    std::erase_if(globals->outputs_, [name](const Output& output) {
        if (output.name != name) return false;
        destroy(output.output);
        return true;
    });
}

void Globals::output_scaled(void* self, wl_output* output, std::int32_t factor) {
    auto* globals = static_cast<Globals*>(self);
    for (Output& known : globals->outputs_) {
        if (known.output == output) known.scale = std::max(1, factor);
    }
}

void Globals::presentation_clock(void* self, wp_presentation* /*presentation*/, std::uint32_t clock) {
    static_cast<Globals*>(self)->presentation_monotonic_ = clock == CLOCK_MONOTONIC;
}

double Globals::output_scale() const noexcept {
    int scale = 1;
    for (const Output& output : outputs_) scale = std::max(scale, output.scale);
    return scale;
}

std::string Globals::describe() const {
    std::string line;
    const auto say = [&line](const char* name, bool found) {
        if (!line.empty()) line += ", ";
        line += name;
        line += found ? " yes" : " no";
    };
    say("wp_fractional_scale_manager_v1", fractional_scale_ != nullptr);
    say("zwp_linux_dmabuf_v1", dmabuf_ != nullptr);
    say("wp_linux_drm_syncobj_manager_v1", syncobj_ != nullptr);
    say("wp_cursor_shape_manager_v1", cursor_shape_ != nullptr);
    say("zwp_pointer_constraints_v1", pointer_constraints_ != nullptr);
    say("zwp_relative_pointer_manager_v1", relative_pointer_ != nullptr);
    say("zwp_keyboard_shortcuts_inhibit_manager_v1", shortcuts_inhibit_ != nullptr);
    say("xdg_activation_v1", activation_ != nullptr);
    say("zxdg_decoration_manager_v1", decoration_ != nullptr);
    say("xdg_toplevel_icon_manager_v1", toplevel_icon_ != nullptr);
    say("wp_presentation (monotonic)", presentation() != nullptr);
    say("wp_alpha_modifier_v1", alpha_modifier_ != nullptr);
    say("xdg_wm_dialog_v1", dialog_ != nullptr);
    return line;
}

}  // namespace rwin::host
