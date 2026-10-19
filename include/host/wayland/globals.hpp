#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_registry;
struct wl_seat;
struct wl_shm;
struct wl_subcompositor;
struct wp_alpha_modifier_v1;
struct wp_cursor_shape_manager_v1;
struct wp_fractional_scale_manager_v1;
struct wp_linux_drm_syncobj_manager_v1;
struct wp_presentation;
struct wp_viewporter;
struct xdg_activation_v1;
struct xdg_toplevel_icon_manager_v1;
struct xdg_wm_base;
struct xdg_wm_dialog_v1;
struct zwp_keyboard_shortcuts_inhibit_manager_v1;
struct zwp_linux_dmabuf_v1;
struct zwp_pointer_constraints_v1;
struct zwp_relative_pointer_manager_v1;
struct zxdg_decoration_manager_v1;

namespace rwin::host {

// The compositor's globals this client speaks, bound once.
//
// Required, and the connection refused without them: wl_compositor (4,
// for damage_buffer), wl_subcompositor, wl_shm, xdg_wm_base, wl_seat and
// wp_viewporter (a mirror is sized apart from its buffer, at any scale).
// Everything else is null where the compositor does not offer it, and
// what needs it does without: shared frames without dma-bufs come as
// pixels, a frame without explicit sync is waited for here, a system
// cursor without shapes is the guest's picture, and so on.
class Globals {
public:
    // ``display`` outlives this. Throws std::runtime_error naming what
    // the compositor lacks.
    explicit Globals(wl_display* display);
    ~Globals();

    Globals(const Globals&) = delete;
    Globals& operator=(const Globals&) = delete;
    Globals(Globals&&) = delete;
    Globals& operator=(Globals&&) = delete;

    [[nodiscard]] wl_display* display() const noexcept { return display_; }
    [[nodiscard]] wl_compositor* compositor() const noexcept { return compositor_; }
    [[nodiscard]] std::uint32_t compositor_version() const noexcept { return compositor_version_; }
    [[nodiscard]] wl_subcompositor* subcompositor() const noexcept { return subcompositor_; }
    [[nodiscard]] wl_shm* shm() const noexcept { return shm_; }
    [[nodiscard]] xdg_wm_base* wm_base() const noexcept { return wm_base_; }
    [[nodiscard]] std::uint32_t wm_base_version() const noexcept { return wm_base_version_; }
    // The seat, bound for whoever listens to it from the start: what it can
    // do is said once, as it is bound.
    [[nodiscard]] wl_seat* bind_seat() const;
    [[nodiscard]] std::uint32_t seat_version() const noexcept { return seat_version_; }
    [[nodiscard]] wp_viewporter* viewporter() const noexcept { return viewporter_; }

    [[nodiscard]] wp_fractional_scale_manager_v1* fractional_scale() const noexcept { return fractional_scale_; }
    [[nodiscard]] zwp_linux_dmabuf_v1* dmabuf() const noexcept { return dmabuf_; }
    [[nodiscard]] std::uint32_t dmabuf_version() const noexcept { return dmabuf_version_; }
    [[nodiscard]] wp_linux_drm_syncobj_manager_v1* syncobj() const noexcept { return syncobj_; }
    [[nodiscard]] wp_cursor_shape_manager_v1* cursor_shape() const noexcept { return cursor_shape_; }
    [[nodiscard]] zwp_pointer_constraints_v1* pointer_constraints() const noexcept { return pointer_constraints_; }
    [[nodiscard]] zwp_relative_pointer_manager_v1* relative_pointer() const noexcept { return relative_pointer_; }
    [[nodiscard]] zwp_keyboard_shortcuts_inhibit_manager_v1* shortcuts_inhibit() const noexcept {
        return shortcuts_inhibit_;
    }
    [[nodiscard]] xdg_activation_v1* activation() const noexcept { return activation_; }
    [[nodiscard]] zxdg_decoration_manager_v1* decoration() const noexcept { return decoration_; }
    [[nodiscard]] xdg_toplevel_icon_manager_v1* toplevel_icon() const noexcept { return toplevel_icon_; }
    // Null too where the compositor's presentation clock is not
    // CLOCK_MONOTONIC, the clock every other stamp is on.
    [[nodiscard]] wp_presentation* presentation() const noexcept {
        return presentation_monotonic_ ? presentation_ : nullptr;
    }
    [[nodiscard]] wp_alpha_modifier_v1* alpha_modifier() const noexcept { return alpha_modifier_; }
    [[nodiscard]] xdg_wm_dialog_v1* dialog() const noexcept { return dialog_; }

    // The largest scale any output has: what a surface is drawn at before
    // the compositor names its own.
    [[nodiscard]] double output_scale() const noexcept;

    // "name version, ..." of every optional global found, and of those not.
    [[nodiscard]] std::string describe() const;

private:
    struct Output {
        std::uint32_t name = 0;
        wl_output* output = nullptr;
        int scale = 1;
    };

    static void announced(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                          std::uint32_t version);
    static void withdrawn(void* self, wl_registry* registry, std::uint32_t name);
    static void output_scaled(void* self, wl_output* output, std::int32_t factor);
    static void presentation_clock(void* self, wp_presentation* presentation, std::uint32_t clock);
    void bind(std::uint32_t name, const char* interface, std::uint32_t version);

    wl_display* display_;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    std::uint32_t compositor_version_ = 0;
    wl_subcompositor* subcompositor_ = nullptr;
    wl_shm* shm_ = nullptr;
    xdg_wm_base* wm_base_ = nullptr;
    std::uint32_t wm_base_version_ = 0;
    std::uint32_t seat_name_ = 0;
    std::uint32_t seat_version_ = 0;
    wp_viewporter* viewporter_ = nullptr;
    wp_fractional_scale_manager_v1* fractional_scale_ = nullptr;
    zwp_linux_dmabuf_v1* dmabuf_ = nullptr;
    std::uint32_t dmabuf_version_ = 0;
    wp_linux_drm_syncobj_manager_v1* syncobj_ = nullptr;
    wp_cursor_shape_manager_v1* cursor_shape_ = nullptr;
    zwp_pointer_constraints_v1* pointer_constraints_ = nullptr;
    zwp_relative_pointer_manager_v1* relative_pointer_ = nullptr;
    zwp_keyboard_shortcuts_inhibit_manager_v1* shortcuts_inhibit_ = nullptr;
    xdg_activation_v1* activation_ = nullptr;
    zxdg_decoration_manager_v1* decoration_ = nullptr;
    xdg_toplevel_icon_manager_v1* toplevel_icon_ = nullptr;
    wp_presentation* presentation_ = nullptr;
    bool presentation_monotonic_ = false;
    wp_alpha_modifier_v1* alpha_modifier_ = nullptr;
    xdg_wm_dialog_v1* dialog_ = nullptr;
    std::vector<Output> outputs_;
};

}  // namespace rwin::host
