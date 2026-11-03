#include "host/wayland/pointer_cursor.hpp"

#include "host/wayland/globals.hpp"
#include "logging/logger.hpp"

#include <cursor-shape-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

namespace {

constexpr int kNothing = 0;
constexpr int kShape = 1;
constexpr int kPicture = 2;

}

PointerCursor::PointerCursor(const Globals& globals)
    : globals_(globals), surface_(globals) {}

PointerCursor::~PointerCursor() {
    if (shapes_ != nullptr) wp_cursor_shape_device_v1_destroy(shapes_);
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
    const std::optional<Point> hotspot = surface_.paint(frame, scale);
    if (!hotspot.has_value()) return;
    wl_pointer_set_cursor(pointer_, serial_, surface_.surface(), hotspot->x, hotspot->y);
}

}
