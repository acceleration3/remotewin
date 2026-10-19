#pragma once

#include "host/wayland/popup_placement.hpp"

#include <cstdint>

struct wl_seat;
struct xdg_popup;
struct xdg_positioner;
struct xdg_surface;

namespace rwin::host {

class Globals;
class MirrorSurface;
class PopupEvents;

// A surface as a popup of another (xdg_popup): placed by the compositor
// from where it is meant to open, flipped and slid against the output it is
// on rather than against the guest's screen. One that grabs takes the
// keyboard and is dismissed by a press outside this client's surfaces.
class PopupRole {
public:
    // ``grab_serial`` is the press it opened from, when it grabs; 0 when it
    // does not.
    PopupRole(const Globals& globals, MirrorSurface& surface, xdg_surface* parent, const PopupPlacement& placement,
              wl_seat* seat, std::uint32_t grab_serial, PopupEvents& events);
    ~PopupRole();

    PopupRole(const PopupRole&) = delete;
    PopupRole& operator=(const PopupRole&) = delete;
    PopupRole(PopupRole&&) = delete;
    PopupRole& operator=(PopupRole&&) = delete;

    [[nodiscard]] xdg_surface* xdg() const noexcept { return xdg_surface_; }
    [[nodiscard]] bool grabs() const noexcept { return grabs_; }
    [[nodiscard]] const PopupPlacement& placement() const noexcept { return placement_; }
    // Whether it can move without being made again (xdg_popup.reposition).
    [[nodiscard]] bool repositions() const noexcept;
    void reposition(const PopupPlacement& placement);

private:
    [[nodiscard]] xdg_positioner* positioner_for(const PopupPlacement& placement) const;

    static void surface_configured(void* self, xdg_surface* surface, std::uint32_t serial);
    static void popup_configured(void* self, xdg_popup* popup, std::int32_t x, std::int32_t y, std::int32_t width,
                                 std::int32_t height);
    static void done(void* self, xdg_popup* popup);
    static void repositioned(void* self, xdg_popup* popup, std::uint32_t token);

    const Globals& globals_;
    MirrorSurface& surface_;
    PopupEvents& events_;
    PopupPlacement placement_;
    xdg_surface* xdg_surface_;
    xdg_popup* popup_ = nullptr;
    Box placed_;
    std::uint32_t reposition_token_ = 0;
    bool configured_ = false;
    bool grabs_ = false;
};

}  // namespace rwin::host
