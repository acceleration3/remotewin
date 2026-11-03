#pragma once

#include "host/cursor/cursor_look.hpp"
#include "host/cursor/cursor_shape.hpp"
#include "host/wayland/cursor_surface.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

struct wl_pointer;
struct wp_cursor_shape_device_v1;

namespace rwin::host {

class Globals;

class PointerCursor {
public:
    explicit PointerCursor(const Globals& globals);
    ~PointerCursor();

    PointerCursor(const PointerCursor&) = delete;
    PointerCursor& operator=(const PointerCursor&) = delete;
    PointerCursor(PointerCursor&&) = delete;
    PointerCursor& operator=(PointerCursor&&) = delete;

    void use(wl_pointer* pointer);
    void entered(std::uint32_t serial) noexcept;

    void wear(CursorShape shape);
    void wear(std::uint64_t id, std::size_t step, const CursorLook::Frame& frame, double scale);
    void wear_nothing();

    [[nodiscard]] bool draws_shapes() const noexcept { return shapes_ != nullptr; }

private:
    struct Worn {
        int kind = 0;
        std::uint32_t shape = 0;
        std::uint64_t id = 0;
        std::size_t step = 0;
        double scale = 0.0;

        [[nodiscard]] bool operator==(const Worn&) const noexcept = default;
    };

    [[nodiscard]] bool already(const Worn& worn);
    void show_picture(const CursorLook::Frame& frame, double scale);

    const Globals& globals_;
    wl_pointer* pointer_ = nullptr;
    wp_cursor_shape_device_v1* shapes_ = nullptr;
    CursorSurface surface_;
    std::uint32_t serial_ = 0;
    std::optional<Worn> worn_;
    std::uint32_t worn_serial_ = 0;
};

}
