#pragma once

#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/wayland/input_serial.hpp"
#include "host/wayland/keyboard_events.hpp"
#include "host/wayland/pointer_cursor.hpp"
#include "host/wayland/pointer_events.hpp"

#include <cstdint>
#include <memory>

struct wl_seat;
struct wl_surface;

namespace rwin::host {

class Globals;

// The user's seat: its pointer and keyboard as they come and go
// (wl_seat.capabilities), the cursor over this client's surfaces, and the
// latest press - what a popup grab, a move or a handed-on activation is
// asked with.
class Seat {
public:
    Seat(EventLoop& loop, const Globals& globals);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;
    Seat(Seat&&) = delete;
    Seat& operator=(Seat&&) = delete;

    [[nodiscard]] wl_seat* seat() const noexcept { return seat_; }
    [[nodiscard]] PointerCursor& cursor() noexcept { return cursor_; }
    // Null while the seat has none.
    [[nodiscard]] wl_pointer* pointer() const noexcept;
    [[nodiscard]] wl_surface* pointer_focus() const noexcept;
    [[nodiscard]] wl_surface* keyboard_focus() const noexcept;
    [[nodiscard]] bool control_alt() const;
    [[nodiscard]] const InputSerial& last_press() const noexcept { return last_press_; }

    // A surface of this client's is going: nothing is told to it again.
    void forget(wl_surface* surface) noexcept;

    // The keyboard came to one of this client's surfaces, or left them all.
    Signal<> keyboard_focus_changed;
    // The seat gained or lost its pointer.
    Signal<> pointer_changed;

private:
    static void capable(void* self, wl_seat* seat, std::uint32_t capabilities);
    static void named(void* self, wl_seat* seat, const char* name);
    void pressed(std::uint32_t serial, wl_surface* surface);

    EventLoop& loop_;
    wl_seat* seat_;
    std::uint32_t version_;
    PointerCursor cursor_;
    std::unique_ptr<PointerEvents> pointer_;
    std::unique_ptr<KeyboardEvents> keyboard_;
    InputSerial last_press_;
};

}  // namespace rwin::host
