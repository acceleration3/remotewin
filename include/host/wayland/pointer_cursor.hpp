#pragma once

#include "host/cursor/cursor_look.hpp"
#include "host/cursor/cursor_shape.hpp"
#include "host/wayland/shm_buffers.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

struct wl_pointer;
struct wl_surface;
struct wp_cursor_shape_device_v1;
struct wp_viewport;

namespace rwin::host {

class Globals;

// What the pointer looks like over this client's surfaces: a cursor the
// compositor draws by name (wp_cursor_shape_v1), the guest's own picture
// on a cursor surface of this client's, or none.
//
// A guest pixel is a device pixel here, and a cursor surface is sized in
// logical units: at a scale of p/q a picture whose edges and hotspot are
// multiples of p is exactly a whole number of logical units, so the
// picture is padded with transparency to that, its surface sized to it
// through a viewport, and its hotspot falls on a whole unit (UnitPadding).
// At a scale that is no small fraction it is shown at the nearest size.
//
// A cursor is set for one entry of the pointer into a surface; each entry
// sets it again.
class PointerCursor {
public:
    explicit PointerCursor(const Globals& globals);
    ~PointerCursor();

    PointerCursor(const PointerCursor&) = delete;
    PointerCursor& operator=(const PointerCursor&) = delete;
    PointerCursor(PointerCursor&&) = delete;
    PointerCursor& operator=(PointerCursor&&) = delete;

    // The seat's pointer, when it has one; null when it has lost it.
    void use(wl_pointer* pointer);
    // The pointer entered a surface of this client's: what is worn from now
    // on is set against this.
    void entered(std::uint32_t serial) noexcept;

    void wear(CursorShape shape);
    // ``frame`` of the cursor ``id``, at ``scale`` device pixels per
    // logical unit of the surface the pointer is over.
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
    wl_surface* surface_ = nullptr;
    wp_viewport* viewport_ = nullptr;
    ShmBuffers buffers_;
    std::uint32_t serial_ = 0;
    // Set against the serial it was set with.
    std::optional<Worn> worn_;
    std::uint32_t worn_serial_ = 0;
};

}  // namespace rwin::host
