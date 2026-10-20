#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/size.hpp"
#include "host/wayland/toplevel_configure.hpp"

#include <cstdint>
#include <string>

struct wl_array;
struct wl_seat;
struct xdg_dialog_v1;
struct xdg_surface;
struct xdg_toplevel;
struct xdg_toplevel_icon_v1;
struct zxdg_toplevel_decoration_v1;

namespace rwin::host {

class Globals;
class MirrorSurface;
class ToplevelEvents;

// A surface as a window of this desktop (xdg_toplevel): the compositor
// places it, sizes it, maximises it, puts it in the taskbar. Its first
// configure is waited for before a buffer is committed to it, and every
// configure is acknowledged before its owner hears of it.
class ToplevelRole {
public:
    // Undecorated - the guest draws its own chrome - unless
    // ``server_decorated``.
    ToplevelRole(const Globals& globals, MirrorSurface& surface, ToplevelEvents& events, bool server_decorated);
    ~ToplevelRole();

    ToplevelRole(const ToplevelRole&) = delete;
    ToplevelRole& operator=(const ToplevelRole&) = delete;
    ToplevelRole(ToplevelRole&&) = delete;
    ToplevelRole& operator=(ToplevelRole&&) = delete;

    [[nodiscard]] xdg_surface* xdg() const noexcept { return xdg_surface_; }
    [[nodiscard]] xdg_toplevel* toplevel() const noexcept { return toplevel_; }
    [[nodiscard]] bool configured() const noexcept { return configured_; }
    [[nodiscard]] const ToplevelConfigure& last() const noexcept { return last_; }

    // The first commit, without a buffer, once everything it starts with is
    // said: the compositor configures it in answer.
    void start();
    void set_title(const std::string& title);
    void set_app_id(const std::string& app_id);
    // Kept above ``parent`` and grouped with it; null for none.
    void set_parent(const ToplevelRole* parent);
    // Logical units; an empty side is no limit.
    void limit(Size smallest, Size largest);
    void set_window_geometry(const Box& geometry);
    void maximize(bool maximized);
    void fullscreen(bool full);
    void minimize();
    // The compositor runs the user's drag (xdg_toplevel.move/resize) for
    // the press of ``serial``; ``edges`` are xdg_toplevel's resize_edge.
    void move(wl_seat* seat, std::uint32_t serial);
    void resize(wl_seat* seat, std::uint32_t serial, std::uint32_t edges);
    // A modal dialog of its parent (xdg_dialog_v1), where the compositor
    // knows of those.
    void set_modal(bool modal);
    void set_icon(xdg_toplevel_icon_v1* icon);

private:
    static void surface_configured(void* self, xdg_surface* surface, std::uint32_t serial);
    static void toplevel_configured(void* self, xdg_toplevel* toplevel, std::int32_t width, std::int32_t height,
                                    wl_array* states);
    static void closed(void* self, xdg_toplevel* toplevel);
    static void bounded(void* self, xdg_toplevel* toplevel, std::int32_t width, std::int32_t height);
    static void capable(void* self, xdg_toplevel* toplevel, wl_array* capabilities);

    const Globals& globals_;
    MirrorSurface& surface_;
    ToplevelEvents& events_;
    xdg_surface* xdg_surface_;
    xdg_toplevel* toplevel_;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    xdg_dialog_v1* dialog_ = nullptr;
    // The parent's serial (0 for none): a role made where a freed one was is
    // a new parent, and is said again.
    std::uint64_t parent_ = 0;
    std::uint64_t serial_;
    ToplevelConfigure pending_;
    ToplevelConfigure last_;
    bool configured_ = false;
    bool modal_ = false;
};

}  // namespace rwin::host
