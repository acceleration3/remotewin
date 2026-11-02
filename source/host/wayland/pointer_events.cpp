#include "host/wayland/pointer_events.hpp"

#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/pointer_cursor.hpp"

#include <wayland-client.h>

#include <cmath>

namespace rwin::host {

namespace {

constexpr std::uint32_t kFrameVersion = 5;
constexpr double kStep = 120.0;
constexpr double kUnitsPerStep = 15.0;

}

PointerEvents::PointerEvents(wl_pointer* pointer, std::uint32_t version, PointerCursor& cursor,
                             std::function<void(std::uint32_t, wl_surface*)> pressed)
    : pointer_(pointer), version_(version), cursor_(cursor), pressed_(std::move(pressed)) {
    static constexpr wl_pointer_listener kListener{
        &PointerEvents::entered,  &PointerEvents::left,    &PointerEvents::moved,     &PointerEvents::pressed,
        &PointerEvents::turned,   &PointerEvents::framed,  &PointerEvents::sourced,   &PointerEvents::stopped,
        &PointerEvents::clicked,  &PointerEvents::turned120, &PointerEvents::directed};
    wl_pointer_add_listener(pointer_, &kListener, this);
}

PointerEvents::~PointerEvents() {
    if (version_ >= WL_POINTER_RELEASE_SINCE_VERSION) {
        wl_pointer_release(pointer_);
    } else {
        wl_pointer_destroy(pointer_);
    }
}

void PointerEvents::forget(wl_surface* surface) noexcept {
    if (focus_ == surface) focus_ = nullptr;
    if (pending_.entered == surface) pending_.entered = nullptr;
}

SurfaceInput* PointerEvents::target() const {
    return MirrorSurface::input_of(focus_);
}

void PointerEvents::entered(void* self, wl_pointer* , std::uint32_t serial, wl_surface* surface,
                            std::int32_t x, std::int32_t y) {
    auto* events = static_cast<PointerEvents*>(self);
    events->cursor_.entered(serial);
    events->pending_.entered = surface;
    events->pending_.at = PointF{wl_fixed_to_double(x), wl_fixed_to_double(y)};
    if (events->version_ < kFrameVersion) events->flush();
}

void PointerEvents::left(void* self, wl_pointer* , std::uint32_t , wl_surface* surface) {
    auto* events = static_cast<PointerEvents*>(self);
    if (surface == events->focus_) events->pending_.left = true;
    if (events->version_ < kFrameVersion) events->flush();
}

void PointerEvents::moved(void* self, wl_pointer* , std::uint32_t , std::int32_t x,
                          std::int32_t y) {
    auto* events = static_cast<PointerEvents*>(self);
    events->pending_.at = PointF{wl_fixed_to_double(x), wl_fixed_to_double(y)};
    if (events->version_ < kFrameVersion) events->flush();
}

void PointerEvents::pressed(void* self, wl_pointer* , std::uint32_t serial, std::uint32_t ,
                            std::uint32_t button, std::uint32_t state) {
    auto* events = static_cast<PointerEvents*>(self);
    const bool down = state == WL_POINTER_BUTTON_STATE_PRESSED;
    if (down) {
        wl_surface* on = events->pending_.entered != nullptr ? events->pending_.entered : events->focus_;
        events->pressed_(serial, on);
    }
    events->pending_.buttons.emplace_back(button, down);
    if (events->version_ < kFrameVersion) events->flush();
}

void PointerEvents::turned(void* self, wl_pointer* , std::uint32_t , std::uint32_t axis,
                           std::int32_t value) {
    auto* events = static_cast<PointerEvents*>(self);
    const double units = wl_fixed_to_double(value) * kStep / kUnitsPerStep;
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        events->pending_.smooth_x += units;
    } else {
        events->pending_.smooth_y += units;
    }
    if (events->version_ < kFrameVersion) events->flush();
}

void PointerEvents::clicked(void* self, wl_pointer* , std::uint32_t axis, std::int32_t discrete) {
    auto* events = static_cast<PointerEvents*>(self);
    events->pending_.stepped = true;
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        events->pending_.v120_x += discrete * kStep;
    } else {
        events->pending_.v120_y += discrete * kStep;
    }
}

void PointerEvents::turned120(void* self, wl_pointer* , std::uint32_t axis, std::int32_t value120) {
    auto* events = static_cast<PointerEvents*>(self);
    events->pending_.stepped = true;
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        events->pending_.v120_x += value120;
    } else {
        events->pending_.v120_y += value120;
    }
}

void PointerEvents::sourced(void* , wl_pointer* , std::uint32_t ) {}
void PointerEvents::stopped(void* , wl_pointer* , std::uint32_t , std::uint32_t ) {}
void PointerEvents::directed(void* , wl_pointer* , std::uint32_t ,
                             std::uint32_t ) {}

void PointerEvents::framed(void* self, wl_pointer* ) { static_cast<PointerEvents*>(self)->flush(); }

void PointerEvents::flush() {
    Pending frame = std::move(pending_);
    pending_ = Pending{};
    if (frame.left) {
        if (SurfaceInput* input = target()) input->pointer_left();
        focus_ = nullptr;
    }
    if (frame.entered != nullptr) {
        focus_ = frame.entered;
        if (SurfaceInput* input = target(); input != nullptr && frame.at.has_value()) input->pointer_entered(*frame.at);
        frame.at.reset();
    }
    if (frame.at.has_value()) {
        if (SurfaceInput* input = target()) input->pointer_moved(*frame.at);
    }
    for (const auto& [code, down] : frame.buttons) {
        if (SurfaceInput* input = target()) input->button(code, down);
    }
    const double x = carried_x_ + (frame.stepped ? frame.v120_x : frame.smooth_x);
    const double y = carried_y_ + (frame.stepped ? frame.v120_y : frame.smooth_y);
    const SurfaceInput::Wheel wheel{static_cast<std::int32_t>(std::trunc(x)), static_cast<std::int32_t>(std::trunc(y))};
    carried_x_ = x - wheel.dx120;
    carried_y_ = y - wheel.dy120;
    if (wheel.dx120 == 0 && wheel.dy120 == 0) return;
    if (SurfaceInput* input = target()) input->wheel(wheel);
}

}
