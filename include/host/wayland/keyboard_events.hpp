#pragma once

#include "host/loop/event_loop.hpp"

#include <cstdint>
#include <functional>
#include <memory>

struct wl_array;
struct wl_keyboard;
struct wl_surface;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace rwin::host {

// The seat's keyboard, told to the surface that has it: each key with what
// the compositor's keymap makes of it, its modifiers as the compositor
// keeps them (wl_keyboard.modifiers), and a held key repeated here at the
// compositor's rate and delay (wl_keyboard.repeat_info) - a Wayland client
// repeats keys itself, and Windows does not repeat a key put into it.
class KeyboardEvents {
public:
    // ``pressed`` hears the serial and surface of every key press;
    // ``moved`` that the keyboard came to a surface or left one.
    KeyboardEvents(EventLoop& loop, wl_keyboard* keyboard, std::uint32_t version,
                   std::function<void(std::uint32_t, wl_surface*)> pressed, std::function<void()> moved);
    ~KeyboardEvents();

    KeyboardEvents(const KeyboardEvents&) = delete;
    KeyboardEvents& operator=(const KeyboardEvents&) = delete;
    KeyboardEvents(KeyboardEvents&&) = delete;
    KeyboardEvents& operator=(KeyboardEvents&&) = delete;

    // The surface with the keyboard, of this client's; null for none.
    [[nodiscard]] wl_surface* focus() const noexcept { return focus_; }
    // Ctrl and Alt both held, as the compositor says.
    [[nodiscard]] bool control_alt() const;
    void forget(wl_surface* surface) noexcept;

private:
    struct XkbRelease {
        void operator()(xkb_context* context) const noexcept;
        void operator()(xkb_keymap* keymap) const noexcept;
        void operator()(xkb_state* state) const noexcept;
    };

    static void mapped(void* self, wl_keyboard* keyboard, std::uint32_t format, std::int32_t fd, std::uint32_t size);
    static void entered(void* self, wl_keyboard* keyboard, std::uint32_t serial, wl_surface* surface,
                        wl_array* keys);
    static void left(void* self, wl_keyboard* keyboard, std::uint32_t serial, wl_surface* surface);
    static void keyed(void* self, wl_keyboard* keyboard, std::uint32_t serial, std::uint32_t time, std::uint32_t key,
                      std::uint32_t state);
    static void modified(void* self, wl_keyboard* keyboard, std::uint32_t serial, std::uint32_t depressed,
                         std::uint32_t latched, std::uint32_t locked, std::uint32_t group);
    static void repeats(void* self, wl_keyboard* keyboard, std::int32_t rate, std::int32_t delay);

    void deliver(std::uint32_t code, bool pressed, bool repeat);
    void repeat();

    wl_keyboard* keyboard_;
    std::uint32_t version_;
    std::function<void(std::uint32_t, wl_surface*)> pressed_;
    std::function<void()> moved_;
    std::unique_ptr<xkb_context, XkbRelease> context_;
    std::unique_ptr<xkb_keymap, XkbRelease> keymap_;
    std::unique_ptr<xkb_state, XkbRelease> state_;
    std::unique_ptr<EventLoop::Timer> repeater_;
    wl_surface* focus_ = nullptr;
    // 0 is no repeat at all.
    std::int32_t rate_ = 25;
    std::int32_t delay_ms_ = 600;
    std::uint32_t repeating_ = 0;
    bool repeating_any_ = false;
};

}  // namespace rwin::host
