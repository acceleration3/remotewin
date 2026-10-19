#include "host/wayland/keyboard_events.hpp"

#include "host/wayland/key_press.hpp"
#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/surface_input.hpp"
#include "logging/logger.hpp"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>

namespace rwin::host {

namespace {

// XKB keycodes are evdev's plus 8, the offset X11 kept below its own.
constexpr std::uint32_t kXkbOffset = 8;
constexpr int kMillisecondsPerSecond = 1000;

SurfaceInput* input_of(wl_surface* surface) { return MirrorSurface::input_of(surface); }

}  // namespace

void KeyboardEvents::XkbRelease::operator()(xkb_context* context) const noexcept { xkb_context_unref(context); }
void KeyboardEvents::XkbRelease::operator()(xkb_keymap* keymap) const noexcept { xkb_keymap_unref(keymap); }
void KeyboardEvents::XkbRelease::operator()(xkb_state* state) const noexcept { xkb_state_unref(state); }

KeyboardEvents::KeyboardEvents(EventLoop& loop, wl_keyboard* keyboard, std::uint32_t version,
                               std::function<void(std::uint32_t, wl_surface*)> pressed, std::function<void()> moved)
    : keyboard_(keyboard), version_(version), pressed_(std::move(pressed)), moved_(std::move(moved)),
      context_(xkb_context_new(XKB_CONTEXT_NO_FLAGS)), repeater_(loop.timer([this] { repeat(); })) {
    static constexpr wl_keyboard_listener kListener{&KeyboardEvents::mapped,   &KeyboardEvents::entered,
                                                    &KeyboardEvents::left,     &KeyboardEvents::keyed,
                                                    &KeyboardEvents::modified, &KeyboardEvents::repeats};
    wl_keyboard_add_listener(keyboard_, &kListener, this);
}

KeyboardEvents::~KeyboardEvents() {
    if (version_ >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
        wl_keyboard_release(keyboard_);
    } else {
        wl_keyboard_destroy(keyboard_);
    }
}

void KeyboardEvents::forget(wl_surface* surface) noexcept {
    if (focus_ != surface) return;
    focus_ = nullptr;
    repeater_->stop();
    repeating_any_ = false;
}

bool KeyboardEvents::control_alt() const {
    if (state_ == nullptr) return false;
    return xkb_state_mod_name_is_active(state_.get(), XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0
           && xkb_state_mod_name_is_active(state_.get(), XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0;
}

void KeyboardEvents::mapped(void* self, wl_keyboard* /*keyboard*/, std::uint32_t format, std::int32_t fd,
                            std::uint32_t size) {
    auto* events = static_cast<KeyboardEvents*>(self);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || events->context_ == nullptr) {
        ::close(fd);
        return;
    }
    void* text = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (text == MAP_FAILED) {
        RWIN_WARN("keyboard", "the compositor's keymap could not be read");
        return;
    }
    events->keymap_.reset(xkb_keymap_new_from_string(events->context_.get(), static_cast<const char*>(text),
                                                     XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS));
    ::munmap(text, size);
    events->state_.reset(events->keymap_ != nullptr ? xkb_state_new(events->keymap_.get()) : nullptr);
    if (events->state_ == nullptr) RWIN_WARN("keyboard", "the compositor's keymap does not compile");
}

void KeyboardEvents::entered(void* self, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/, wl_surface* surface,
                             wl_array* /*keys*/) {
    auto* events = static_cast<KeyboardEvents*>(self);
    events->focus_ = surface;
    if (SurfaceInput* input = input_of(surface)) input->keyboard_entered();
    events->moved_();
}

void KeyboardEvents::left(void* self, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/, wl_surface* surface) {
    auto* events = static_cast<KeyboardEvents*>(self);
    events->repeater_->stop();
    events->repeating_any_ = false;
    if (events->focus_ == surface) events->focus_ = nullptr;
    if (SurfaceInput* input = input_of(surface)) input->keyboard_left();
    events->moved_();
}

void KeyboardEvents::keyed(void* self, wl_keyboard* /*keyboard*/, std::uint32_t serial, std::uint32_t /*time*/,
                           std::uint32_t key, std::uint32_t state) {
    auto* events = static_cast<KeyboardEvents*>(self);
    const bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    if (down) events->pressed_(serial, events->focus_);
    events->deliver(key, down, false);
    if (!down) {
        if (events->repeating_any_ && events->repeating_ == key) {
            events->repeater_->stop();
            events->repeating_any_ = false;
        }
        return;
    }
    const bool repeats = events->keymap_ != nullptr && events->rate_ > 0
                         && xkb_keymap_key_repeats(events->keymap_.get(), key + kXkbOffset) != 0;
    if (!repeats) return;
    events->repeating_ = key;
    events->repeating_any_ = true;
    events->repeater_->start(std::chrono::milliseconds(events->delay_ms_), false);
}

void KeyboardEvents::modified(void* self, wl_keyboard* /*keyboard*/, std::uint32_t /*serial*/,
                              std::uint32_t depressed, std::uint32_t latched, std::uint32_t locked,
                              std::uint32_t group) {
    auto* events = static_cast<KeyboardEvents*>(self);
    if (events->state_ != nullptr) xkb_state_update_mask(events->state_.get(), depressed, latched, locked, 0, 0, group);
}

void KeyboardEvents::repeats(void* self, wl_keyboard* /*keyboard*/, std::int32_t rate, std::int32_t delay) {
    auto* events = static_cast<KeyboardEvents*>(self);
    events->rate_ = rate;
    events->delay_ms_ = delay;
}

void KeyboardEvents::repeat() {
    if (!repeating_any_ || rate_ <= 0) return;
    deliver(repeating_, true, true);
    repeater_->start(std::chrono::milliseconds(std::max(1, kMillisecondsPerSecond / rate_)), false);
}

void KeyboardEvents::deliver(std::uint32_t code, bool pressed, bool repeat) {
    KeyPress press{code, pressed, repeat, 0, 0};
    if (state_ != nullptr) {
        press.keysym = xkb_state_key_get_one_sym(state_.get(), code + kXkbOffset);
        press.character = xkb_state_key_get_utf32(state_.get(), code + kXkbOffset);
    }
    if (SurfaceInput* input = input_of(focus_)) input->key(press);
}

}  // namespace rwin::host
