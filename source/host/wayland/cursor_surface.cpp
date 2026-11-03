#include "host/wayland/cursor_surface.hpp"

#include "host/cursor/unit_padding.hpp"
#include "host/wayland/globals.hpp"

#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rwin::host {

namespace {

void premultiplied_into(std::uint8_t* to, const std::uint8_t* from, std::size_t pixels) {
    for (std::size_t at = 0; at < pixels; ++at, to += 4, from += 4) {
        const unsigned alpha = from[3];
        to[0] = static_cast<std::uint8_t>((from[0] * alpha + 127) / 255);
        to[1] = static_cast<std::uint8_t>((from[1] * alpha + 127) / 255);
        to[2] = static_cast<std::uint8_t>((from[2] * alpha + 127) / 255);
        to[3] = static_cast<std::uint8_t>(alpha);
    }
}

}

CursorSurface::CursorSurface(const Globals& globals)
    : surface_(wl_compositor_create_surface(globals.compositor())),
      viewport_(wp_viewporter_get_viewport(globals.viewporter(), surface_)), buffers_(globals.shm()) {}

CursorSurface::~CursorSurface() {
    wp_viewport_destroy(viewport_);
    wl_surface_destroy(surface_);
}

std::optional<Point> CursorSurface::paint(const CursorLook::Frame& frame, double scale) {
    const std::optional<UnitPadding> padding = unit_padding(frame.size, frame.hotspot, scale);
    const Size canvas = padding.has_value() ? padding->padded : frame.size;
    const Point offset = padding.has_value() ? padding->offset : Point{};
    ShmBuffer* buffer = buffers_.free_buffer(canvas);
    if (buffer == nullptr) return std::nullopt;
    std::span<std::uint8_t> pixels = buffer->pixels();
    std::fill(pixels.begin(), pixels.end(), std::uint8_t{0});
    for (int row = 0; row < frame.size.height; ++row) {
        premultiplied_into(pixels.data() + static_cast<std::size_t>(row + offset.y) * buffer->stride() + offset.x * 4,
                           frame.bgra.data() + static_cast<std::size_t>(row) * frame.size.width * 4,
                           static_cast<std::size_t>(frame.size.width));
    }
    const Size logical = padding.has_value()
                             ? padding->logical
                             : Size{std::max(1, static_cast<int>(std::lround(frame.size.width / scale))),
                                    std::max(1, static_cast<int>(std::lround(frame.size.height / scale)))};
    const Point hotspot = padding.has_value() ? padding->logical_hotspot
                                              : Point{static_cast<int>(std::lround(frame.hotspot.x / scale)),
                                                      static_cast<int>(std::lround(frame.hotspot.y / scale))};
    wp_viewport_set_destination(viewport_, logical.width, logical.height);
    wl_surface_attach(surface_, buffer->buffer(), 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, canvas.width, canvas.height);
    buffer->attached();
    wl_surface_commit(surface_);
    return hotspot;
}

void CursorSurface::clear() {
    wl_surface_attach(surface_, nullptr, 0, 0);
    wl_surface_commit(surface_);
}

}
