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
#include "host/mirror/mirror_kit.hpp"
#include "host/mirror/mirror_role.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/size_requests.hpp"
#include "host/wayland/backdrop.hpp"
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

// One guest window, as a surface of this desktop's in the role its Win32
// words and its owner give it (MirrorRole): a toplevel, a popup placed
// where it was meant to open, or a part of the window it belongs to.
//
// The guest's chrome is in the guest's pixels, and nothing of it is
// interpreted here: a press on it is passed on, and when Windows begins
// moving or sizing the window the guest says so and the compositor is
// handed the gesture. Everything else is read off the window's words and
// state: whether its edges resize, whether it is minimised, maximised or
// fullscreen, whether it takes input at all.
//
// Input is the guest's at the same point of the same window: a point of
// this surface is a point of this window's picture, whichever window of the
// guest is on top there - the guest raises this one first (R2).
//
// A role cannot change on a surface, so a new role, or a new parent, is a
// new surface.
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
    // A surface is made for it.
    [[nodiscard]] bool shown() const noexcept { return surface_ != nullptr; }
    // A buffer is committed to that surface: it can be a parent.
    [[nodiscard]] bool mapped() const noexcept { return surface_ != nullptr && surface_->mapped(); }
    [[nodiscard]] wl_surface* surface() const noexcept;
    // The mirror whose xdg surface a popup of this one is made against:
    // this one, or for a subsurface its parent's, with ``offset`` growing by
    // where each subsurface is on its parent.
    [[nodiscard]] const WindowMirror* xdg_owner(Point& offset) const noexcept;
    [[nodiscard]] xdg_surface* xdg() const noexcept;
    // A popup that took the seat's grab.
    [[nodiscard]] bool grabs() const noexcept { return popup_ != nullptr && popup_->grabs(); }
    [[nodiscard]] ViewMapping mapping() const noexcept;
    // The guest picture shown, in its pixels: the frame shown, or the size
    // the guest last said before there is one.
    [[nodiscard]] Size picture_size() const noexcept;
    [[nodiscard]] double scale() const noexcept;
    // The compositor has said the scale of its surface: a size in logical
    // units is the one it will have.
    [[nodiscard]] bool scale_known() const noexcept { return surface_ != nullptr && surface_->scale_known(); }
    [[nodiscard]] std::uint32_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool holds_press() const noexcept { return buttons_held_ > 0; }

    // Shown as ``role``, against ``parent``'s mirror (null for none): made
    // again when either changed, or when the parent's surface is new.
    void show_as(const MirrorRole& role, WindowMirror* parent);
    // Hidden in the guest: its surface goes.
    void hide();
    // What it was placed against is going: its surface goes with it, and
    // it stands on its own until arranged again.
    void orphan();
    // Its popup taken down for another's sake (PopupStack): made again by
    // whoever shows it next.
    void drop_popup();

    // The window as the guest last said it.
    void follow(const GuestWindow& window);
    void show_frame(const PixelsView& pixels);
    void show_frame(const SharedFrame& frame);
    // The guest's cursor, at a step of its animation; null is none.
    void wear_cursor(const CursorLook* look, std::size_t step);
    void set_icon(const WindowIcon& icon);
    // Whether the guest has this as its active window.
    void note_guest_active(bool active);
    // Activated with ``token``, or with one asked for as the user's latest
    // press when it is empty.
    void activate(const std::string& token);
    // Windows began moving or sizing this window (MoveSizeStart). When a
    // press here began it, the compositor is asked to run that gesture
    // instead, and the guest is given the press's release at once: Windows'
    // own loop ends where it began, having moved nothing.
    void take_over_move_size(MoveSizeKind kind);
    // While a button is held here, the pointer's own motion moves the
    // guest's pointer from where the press was (PointerAt): a window the
    // guest moves under the pointer - a panel being dragged - is not
    // measured against a surface a frame behind it.
    void follow_relative_motion(bool follows) noexcept { relative_motion_ = follows; }
    void held_motion(PointF logical);

    [[nodiscard]] MirrorState state() const;

    // The keyboard came here; ``by_press`` when a press in a mirror brought
    // it, which the guest has already activated the window by.
    Signal<WindowId, bool> focused;
    Signal<WindowId> pressed_while_disabled;
    // Frames on the GPU cannot be shown here: why.
    Signal<WindowId, const std::string&> shared_refused;
    Signal<const FrameStamps&> presented;
    // The last button held here was let go of.
    Signal<WindowId> let_go;
    // A new surface was made, which the compositor places where it likes.
    Signal<WindowId> remade;
    // Its popup was taken down for another's (PopupStack), or the surface
    // it was a part of went: it is to be shown again.
    Signal<WindowId> dropped;
    // It now has another size or scale: what is placed against it is placed
    // again.
    Signal<WindowId> reshaped;
    // A buffer was first committed to its surface.
    Signal<WindowId> mapped_now;

private:
    // SurfaceInput.
    void pointer_entered(PointF at) override;
    void pointer_moved(PointF at) override;
    void pointer_left() override;
    void button(std::uint32_t code, bool pressed) override;
    void wheel(Wheel turned) override;
    void keyboard_entered() override;
    void keyboard_left() override;
    void key(const KeyPress& press) override;
    // ToplevelEvents.
    void configured(const ToplevelConfigure& configure) override;
    void close_requested() override;
    // PopupEvents.
    void popup_placed(const Box& where) override;
    void popup_dismissed() override;

    void make_surface();
    void make_role();
    void drop_surface();
    void dress_toplevel();
    void place_popup();
    void place_subsurface();
    void picture_changed();
    void apply_limits();
    void follow_state();
    void fit_into(const ToplevelConfigure& configure);
    void ask_guest_for(Size picture);
    void apply_cursor();
    [[nodiscard]] bool covers_monitor() const noexcept;
    [[nodiscard]] std::optional<Point> picture_point(PointF at) const noexcept;
    [[nodiscard]] Size logical_of(Size picture) const noexcept;
    void send_button(std::uint8_t button, Point at, bool pressed);
    [[nodiscard]] bool pressed_into() const noexcept;
    void show_last_frame();
    // The colour Windows does not draw (LWA_COLORKEY), if the window has one.
    [[nodiscard]] std::optional<std::uint32_t> color_key() const;
    void dismiss_in_guest();

    MirrorKit kit_;
    GuestWindow window_;
    Win32Style style_;
    MirrorRole role_;
    WindowMirror* parent_ = nullptr;
    // The parent's surface this one was made against.
    wl_surface* made_against_ = nullptr;
    std::unique_ptr<MirrorSurface> surface_;
    std::unique_ptr<ToplevelRole> toplevel_;
    std::unique_ptr<PopupRole> popup_;
    std::unique_ptr<SubsurfaceRole> subsurface_;
    std::unique_ptr<Backdrop> backdrop_;
    std::unique_ptr<ToplevelIcon> icon_;
    std::unique_ptr<ShortcutsInhibitor> inhibitor_;
    SizeRequests sizes_;
    ResizeBorder border_;
    HeldKeys held_keys_;
    std::optional<MinMax> limits_;
    ToplevelConfigure configure_;
    // What this side asked of the compositor for the guest window's state.
    bool asked_minimized_ = false;
    bool asked_maximized_ = false;
    bool asked_fullscreen_ = false;
    // Minimised in the guest before the compositor first configured it.
    bool minimize_owed_ = false;
    bool guest_active_ = false;
    Box placed_;
    // The guest's cursor, as last worn; hidden when the guest has none.
    const CursorLook* look_ = nullptr;
    std::size_t step_ = 0;
    bool cursor_hidden_ = false;
    // Where the pointer is over this surface; nothing while it is not.
    std::optional<PointF> pointer_;
    std::uint32_t hovered_edges_ = 0;
    int buttons_held_ = 0;
    // The last press passed on, in this window's picture: where a gesture
    // the compositor takes over from Windows lets the button go.
    Point pressed_at_;
    std::uint8_t pressed_button_ = 0;
    // The press that began a resize from the band was the compositor's; its
    // release is not the guest's.
    bool swallow_release_ = false;
    // While a button pressed here is held, where the guest's pointer is, on
    // its screen: the press, plus the pointer's motion since.
    std::optional<PointF> held_at_;
    bool relative_motion_ = false;
    std::uint64_t frames_ = 0;
    std::uint32_t generation_ = 0;
    // The last frame, shown again on a surface made anew: the guest sends
    // one only when its window changes. Pixels are kept in a buffer of this
    // mirror's own, reused frame to frame.
    std::optional<SharedFrame> last_shared_;
    std::vector<std::uint8_t> last_pixels_;
    Size last_pixels_size_;
    // The compositor dismissed its popup; not made again until the guest
    // hides and shows it.
    bool dismissed_ = false;
    std::int64_t dismissed_at_ = 0;
    // Waits to see whether the press that dismissed the popup was one of
    // this application's, which the guest closes the popup for by itself.
    std::unique_ptr<EventLoop::Timer> dismissal_;
};

}  // namespace rwin::host
