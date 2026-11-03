#pragma once

#include "host/cursor/cursor_look.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/wayland/cursor_surface.hpp"

#include <cstddef>
#include <cstdint>

struct wl_subsurface;
struct wl_surface;

namespace rwin::host {

class Globals;

class CursorOverlay {
public:
    CursorOverlay(const Globals& globals, wl_surface* parent);
    ~CursorOverlay();

    CursorOverlay(const CursorOverlay&) = delete;
    CursorOverlay& operator=(const CursorOverlay&) = delete;
    CursorOverlay(CursorOverlay&&) = delete;
    CursorOverlay& operator=(CursorOverlay&&) = delete;

    [[nodiscard]] bool show(const CursorLook& look, std::size_t step, double scale, PointF at);
    void hide();

private:
    CursorSurface surface_;
    wl_subsurface* subsurface_;
    bool painted_ = false;
    std::uint64_t id_ = 0;
    std::size_t step_ = 0;
    double scale_ = 0.0;
    Point hotspot_;
    Point placed_;
    bool positioned_ = false;
};

}
