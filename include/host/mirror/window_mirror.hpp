#pragma once

#include "host/chrome/resize_border.hpp"
#include "host/chrome/win32_style.hpp"
#include "host/cursor/cursor_look.hpp"
#include "host/export/shared_frame.hpp"
#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
#include "host/geometry/view_mapping.hpp"
#include "host/input/held_keys.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/pixels_view.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/mirror/focus_cause.hpp"
#include "host/mirror/mirror_kit.hpp"
#include "host/mirror/mirror_role.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/move_requests.hpp"
#include "host/mirror/state_sync.hpp"
#include "host/wayland/backdrop.hpp"
#include "host/wayland/cursor_overlay.hpp"
#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/popup_events.hpp"
#include "host/wayland/popup_role.hpp"
#include "host/wayland/shortcuts_inhibitor.hpp"
#include "host/wayland/subsurface_role.hpp"
#include "host/wayland/surface_input.hpp"
#include "host/wayland/toplevel_events.hpp"
#include "host/wayland/toplevel_icon.hpp"
#include "host/wayland/toplevel_role.hpp"
#include "proto/min_max.hpp"
#include "proto/move_size_kind.hpp"
#include "proto/window_id.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct wl_surface;
struct xdg_surface;

namespace rwin {
struct WindowIcon;
}

namespace rwin::host {
class WindowMirror final : public SurfaceInput, public ToplevelEvents, public PopupEvents {
public:
    WindowMirror(const MirrorKit& kit, const GuestWindow& window);
    ~WindowMirror();

    WindowMirror(const WindowMirror&) = delete;
    WindowMirror& operator=(const WindowMirror&) = delete;
    WindowMirror(WindowMirror&&) = delete;
    WindowMirror& operator=(WindowMirror&&) = delete;

    [[nodiscard]] WindowId window() const noexcept { return window_.id(); }
    [[nodiscard]] const MirrorRole& role() const noexcept { return role_; }
    [[nodiscard]] WindowMirror* parent() const noexcept { return parent_; }
    [[nodiscard]] bool shown() const noexcept { return surface_ != nullptr; }
    [[nodiscard]] bool mapped() const noexcept { return surface_ != nullptr && surface_->mapped(); }
    [[nodiscard]] wl_surface* surface() const noexcept;
    [[nodiscard]] const WindowMirror* xdg_owner(Point& offset) const noexcept;
    [[nodiscard]] xdg_surface* xdg() const noexcept;
    [[nodiscard]] bool grabs() const noexcept { return popup_ != nullptr && popup_->grabs(); }
    [[nodiscard]] ViewMapping mapping() const noexcept;
    [[nodiscard]] Size picture_size() const noexcept;
    [[nodiscard]] double scale() const noexcept;
    [[nodiscard]] bool scale_known() const noexcept { return surface_ != nullptr && surface_->scale_known(); }
    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool holds_press() const noexcept { return buttons_held_ > 0; }
    [[nodiscard]] bool guest_catching_up() const noexcept { return !moves_.idle(); }

    void show_as(const MirrorRole& role, WindowMirror* parent);
    void hide();
    void orphan();
    void drop_popup();

    void follow(const GuestWindow& window);
    void show_frame(const PixelsView& pixels);
    void show_frame(const SharedFrame& frame);
    void wear_cursor(const CursorLook* look, std::size_t step);
    void set_icon(const WindowIcon& icon);
    void note_guest_active(bool active);
    void activate(const std::string& token);
    void take_over_move_size(MoveSizeKind kind);
    void move_guest_to(Point picture_corner);
    void compositor_minimized(bool minimized);
    void follow_relative_motion(bool follows) noexcept { relative_motion_ = follows; }
    void held_motion(PointF logical);
    void hold_shortcuts_for_lock(bool held);
    void guest_cursor_at(Point screen);

    [[nodiscard]] MirrorState state() const;

    Signal<WindowId, FocusCause> focused;
    Signal<WindowId> pressed_while_disabled;
    Signal<WindowId> button_pressed;
    Signal<WindowId> drag_handed;
    Signal<WindowId> resized_by_guest;
    Signal<WindowId, const std::string&> shared_refused;
    Signal<const FrameStamps&> presented;
    Signal<WindowId> let_go;
    Signal<WindowId> remade;
    Signal<WindowId> dropped;
    Signal<WindowId> reshaped;
    Signal<WindowId> mapped_now;

private:
    void pointer_entered(PointF at) override;
    void pointer_moved(PointF at) override;
    void pointer_left() override;
    void button(std::uint32_t code, bool pressed) override;
    void wheel(Wheel turned) override;
    void keyboard_entered() override;
    void keyboard_left() override;
    void key(const KeyPress& press) override;
    void configured(const ToplevelConfigure& configure) override;
    void close_requested() override;
    void popup_placed(const Box& where) override;
    void popup_dismissed() override;

    void make_surface();
    void make_role();
    void drop_surface();
    void dress_toplevel();
    void place_popup();
    void place_subsurface();
    void picture_changed();
    void resized_in_guest();
    void rescaled();
    void apply_limits();
    void follow_state();
    void minimize_here(bool minimized);
    void take_size(const ToplevelConfigure& configure, const ToplevelConfigure& before);
    void ask_for_wanted();
    void answered(const Box& asked, const Box& got);
    void fit_into(const ToplevelConfigure& configure);
    void fit_given(Size given, Size natural);
    void ask_guest_for(const Box& frame);
    [[nodiscard]] Size shown_picture() const noexcept;
    [[nodiscard]] Size window_size() const noexcept;
    void apply_cursor();
    void update_overlay();
    [[nodiscard]] bool covers_monitor() const noexcept;
    void inhibit_shortcuts();
    [[nodiscard]] std::optional<Point> picture_point(PointF at) const noexcept;
    [[nodiscard]] Size logical_of(Size picture) const noexcept;
    [[nodiscard]] Size logical_limit(Size picture) const noexcept;
    void send_button(std::uint8_t button, Point at, bool pressed);
    [[nodiscard]] bool pressed_into() const noexcept;
    [[nodiscard]] FocusCause focus_cause() noexcept;
    [[nodiscard]] std::string caption() const;
    void show_last_frame();
    void dismiss_in_guest();

    MirrorKit kit_;
    GuestWindow window_;
    Win32Style style_;
    MirrorRole role_;
    WindowMirror* parent_ = nullptr;
    std::uint64_t surface_serial_ = 0;
    std::uint64_t made_against_ = 0;
    std::unique_ptr<MirrorSurface> surface_;
    std::unique_ptr<ToplevelRole> toplevel_;
    std::unique_ptr<PopupRole> popup_;
    std::unique_ptr<SubsurfaceRole> subsurface_;
    std::unique_ptr<Backdrop> backdrop_;
    std::unique_ptr<ToplevelIcon> icon_;
    std::unique_ptr<ShortcutsInhibitor> inhibitor_;
    bool lock_holds_shortcuts_ = false;
    std::unique_ptr<CursorOverlay> overlay_;
    std::optional<Point> guest_cursor_;
    MoveRequests moves_;
    StateSync maximize_;
    StateSync minimize_;
    ResizeBorder border_;
    HeldKeys held_keys_;
    std::optional<MinMax> limits_;
    ToplevelConfigure configure_;
    std::optional<Size> wanted_;
    Size picture_;
    std::uint32_t resize_edges_ = 0;
    bool asked_fullscreen_ = false;
    bool minimize_owed_ = false;
    bool guest_active_ = false;
    Box placed_;
    Point geometry_corner_;
    const CursorLook* look_ = nullptr;
    std::size_t step_ = 0;
    bool cursor_hidden_ = false;
    std::optional<PointF> pointer_;
    std::uint32_t hovered_edges_ = 0;
    int buttons_held_ = 0;
    Point pressed_at_;
    std::uint8_t pressed_button_ = 0;
    bool swallow_release_ = false;
    std::optional<PointF> held_at_;
    bool relative_motion_ = false;
    std::uint64_t frames_ = 0;
    std::uint32_t generation_ = 0;
    std::optional<SharedFrame> last_shared_;
    std::vector<std::uint8_t> last_pixels_;
    Size last_pixels_size_;
    bool dismissed_ = false;
    std::int64_t dismissed_at_ = 0;
    std::int64_t mapped_at_ = 0;
    bool focused_since_mapped_ = false;
    std::string caption_;
    std::unique_ptr<EventLoop::Timer> dismissal_;
};

}
