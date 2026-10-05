#pragma once

#include <QString>

#include <cstdint>
#include <memory>

class QWindow;
struct wl_compositor;
struct wl_pointer;
struct wl_registry;
struct wl_surface;
struct wp_viewporter;
struct zwp_pointer_constraints_v1;
struct zwp_relative_pointer_manager_v1;

namespace rwin::host {

// The compositor's pointer globals that Qt does not speak, bound by this
// client itself on Qt's own connection: pointer constraints (lock,
// confine), the relative pointer (unaccelerated motion) and the viewporter
// (a surface's size apart from its buffer's). Each is null where the
// compositor does not offer it, and all are where this is not Wayland at
// all - X11, or offscreen - which every user of them takes as "cannot".
class PointerGlobals {
public:
    PointerGlobals();
    ~PointerGlobals();

    PointerGlobals(const PointerGlobals&) = delete;
    PointerGlobals& operator=(const PointerGlobals&) = delete;
    PointerGlobals(PointerGlobals&&) = delete;
    PointerGlobals& operator=(PointerGlobals&&) = delete;

    [[nodiscard]] zwp_pointer_constraints_v1* constraints() const noexcept;
    [[nodiscard]] zwp_relative_pointer_manager_v1* relative() const noexcept;
    [[nodiscard]] wp_viewporter* viewporter() const noexcept;
    // Whether the compositor draws cursors by name (wp_cursor_shape_v1): Qt
    // then uses its cursor surface for pixel cursors only.
    [[nodiscard]] bool cursor_shapes() const noexcept { return cursor_shapes_; }

    // Qt's, on its connection; null where there is none.
    [[nodiscard]] static wl_compositor* compositor();
    [[nodiscard]] static wl_pointer* pointer();
    [[nodiscard]] static wl_surface* surface_of(QWindow* window);

    // What was found, for the log and a reader of a report.
    [[nodiscard]] const QString& found() const noexcept { return found_; }

private:
    struct Bound;

    static void announce(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                         std::uint32_t version);
    static void withdraw(void* self, wl_registry* registry, std::uint32_t name);

    std::unique_ptr<Bound> bound_;
    bool cursor_shapes_ = false;
    QString found_;
};

}  // namespace rwin::host
