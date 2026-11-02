#pragma once

#include "host/geometry/point_f.hpp"
#include "host/wayland/surface_input.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

struct wl_pointer;
struct wl_surface;

namespace rwin::host {

class PointerCursor;

class PointerEvents {
public:
    PointerEvents(wl_pointer* pointer, std::uint32_t version, PointerCursor& cursor,
                  std::function<void(std::uint32_t, wl_surface*)> pressed);
    ~PointerEvents();

    PointerEvents(const PointerEvents&) = delete;
    PointerEvents& operator=(const PointerEvents&) = delete;
    PointerEvents(PointerEvents&&) = delete;
    PointerEvents& operator=(PointerEvents&&) = delete;

    [[nodiscard]] wl_pointer* pointer() const noexcept { return pointer_; }
    [[nodiscard]] wl_surface* focus() const noexcept { return focus_; }
    void forget(wl_surface* surface) noexcept;

private:
    struct Pending {
        bool left = false;
        wl_surface* entered = nullptr;
        std::optional<PointF> at;
        std::vector<std::pair<std::uint32_t, bool>> buttons;
        double v120_x = 0.0;
        double v120_y = 0.0;
        double smooth_x = 0.0;
        double smooth_y = 0.0;
        bool stepped = false;
    };

    static void entered(void* self, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface,
                        std::int32_t x, std::int32_t y);
    static void left(void* self, wl_pointer* pointer, std::uint32_t serial, wl_surface* surface);
    static void moved(void* self, wl_pointer* pointer, std::uint32_t time, std::int32_t x, std::int32_t y);
    static void pressed(void* self, wl_pointer* pointer, std::uint32_t serial, std::uint32_t time,
                        std::uint32_t button, std::uint32_t state);
    static void turned(void* self, wl_pointer* pointer, std::uint32_t time, std::uint32_t axis, std::int32_t value);
    static void framed(void* self, wl_pointer* pointer);
    static void sourced(void* self, wl_pointer* pointer, std::uint32_t source);
    static void stopped(void* self, wl_pointer* pointer, std::uint32_t time, std::uint32_t axis);
    static void clicked(void* self, wl_pointer* pointer, std::uint32_t axis, std::int32_t discrete);
    static void turned120(void* self, wl_pointer* pointer, std::uint32_t axis, std::int32_t value120);
    static void directed(void* self, wl_pointer* pointer, std::uint32_t axis, std::uint32_t direction);

    [[nodiscard]] SurfaceInput* target() const;
    void flush();

    wl_pointer* pointer_;
    std::uint32_t version_;
    PointerCursor& cursor_;
    std::function<void(std::uint32_t, wl_surface*)> pressed_;
    wl_surface* focus_ = nullptr;
    Pending pending_;
    double carried_x_ = 0.0;
    double carried_y_ = 0.0;
};

}
