#include "host/wayland/pointer_cursor.hpp"

#include "host/cursor/unit_padding.hpp"
#include "host/wayland/globals.hpp"
#include "logging/logger.hpp"

#include <cursor-shape-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rwin::host {

namespace {

constexpr int kNothing = 0;
constexpr int kShape = 1;
constexpr int kPicture = 2;

// Straight alpha, as Windows keeps a cursor, to the premultiplied alpha a
// compositor reads.
void premultiplied_into(std::uint8_t* to, const std::uint8_t* from, std::size_t pixels) {
    for (std::size_t at = 0; at < pixels; ++at, to += 4, from += 4) {
        const unsigned alpha = from[3];
        to[0] = static_cast<std::uint8_t>((from[0] * alpha + 127) / 255);
        to[1] = static_cast<std::uint8_t>((from[1] * alpha + 127) / 255);
        to[2] = static_cast<std::uint8_t>((from[2] * alpha + 127) / 255);
        to[3] = static_cast<std::uint8_t>(alpha);
    }
}

}  // namespace

PointerCursor::PointerCursor(const Globals& globals)
    : globals_(globals), surface_(wl_compositor_create_surface(globals.compositor())),
      viewport_(wp_viewporter_get_viewport(globals.viewporter(), surface_)), buffers_(globals.shm()) {}

PointerCursor::~PointerCursor() {
    if (shapes_ != nullptr) wp_cursor_shape_device_v1_destroy(shapes_);
    wp_viewport_destroy(viewport_);
    wl_surface_destroy(surface_);
}

void PointerCursor::use(wl_pointer* pointer) {
    if (shapes_ != nullptr) {
        wp_cursor_shape_device_v1_destroy(shapes_);
        shapes_ = nullptr;
    }
    pointer_ = pointer;
    worn_.reset();
    if (pointer_ != nullptr && globals_.cursor_shape() != nullptr) {
        shapes_ = wp_cursor_shape_manager_v1_get_pointer(globals_.cursor_shape(), pointer_);
    }
}

void PointerCursor::entered(std::uint32_t serial) noexcept { serial_ = serial; }

bool PointerCursor::already(const Worn& worn) {
    if (worn_ == worn && worn_serial_ == serial_) return true;
    worn_ = worn;
    worn_serial_ = serial_;
    return false;
}

void PointerCursor::wear(CursorShape shape) {
    if (pointer_ == nullptr || already(Worn{kShape, static_cast<std::uint32_t>(shape), 0, 0, 0.0})) return;
    if (shapes_ == nullptr) return;
    wp_cursor_shape_device_v1_set_shape(shapes_, serial_, static_cast<std::uint32_t>(shape));
}

void PointerCursor::wear(std::uint64_t id, std::size_t step, const CursorLook::Frame& frame, double scale) {
    if (pointer_ == nullptr || already(Worn{kPicture, 0, id, step, scale})) return;
    show_picture(frame, scale);
}

void PointerCursor::wear_nothing() {
    if (pointer_ == nullptr || already(Worn{kNothing, 0, 0, 0, 0.0})) return;
    wl_pointer_set_cursor(pointer_, serial_, nullptr, 0, 0);
}

void PointerCursor::show_picture(const CursorLook::Frame& frame, double scale) {
    const std::optional<UnitPadding> padding = unit_padding(frame.size, frame.hotspot, scale);
    const Size canvas = padding.has_value() ? padding->padded : frame.size;
    const Point offset = padding.has_value() ? padding->offset : Point{};
    ShmBuffer* buffer = buffers_.free_buffer(canvas);
    if (buffer == nullptr) return;
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
    wl_pointer_set_cursor(pointer_, serial_, surface_, hotspot.x, hotspot.y);
}

}  // namespace rwin::host
