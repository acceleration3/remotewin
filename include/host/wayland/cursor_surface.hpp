#pragma once

#include "host/cursor/cursor_look.hpp"
#include "host/geometry/point.hpp"
#include "host/wayland/shm_buffers.hpp"

#include <optional>

struct wl_surface;
struct wp_viewport;

namespace rwin::host {

class Globals;

class CursorSurface {
public:
    explicit CursorSurface(const Globals& globals);
    ~CursorSurface();

    CursorSurface(const CursorSurface&) = delete;
    CursorSurface& operator=(const CursorSurface&) = delete;
    CursorSurface(CursorSurface&&) = delete;
    CursorSurface& operator=(CursorSurface&&) = delete;

    [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }

    [[nodiscard]] std::optional<Point> paint(const CursorLook::Frame& frame, double scale);
    void clear();

private:
    wl_surface* surface_;
    wp_viewport* viewport_;
    ShmBuffers buffers_;
};

}
