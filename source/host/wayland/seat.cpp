#include "host/wayland/seat.hpp"

#include "host/latency/frame_stamps.hpp"
#include "host/wayland/globals.hpp"
#include "logging/logger.hpp"

#include <wayland-client.h>

namespace rwin::host {

Seat::Seat(EventLoop& loop, const Globals& globals)
    : loop_(loop), seat_(globals.bind_seat()), version_(globals.seat_version()), cursor_(globals) {
    static constexpr wl_seat_listener kListener{&Seat::capable, &Seat::named};
    wl_seat_add_listener(seat_, &kListener, this);
}

Seat::~Seat() {
    cursor_.use(nullptr);
    pointer_.reset();
    keyboard_.reset();
    if (version_ >= WL_SEAT_RELEASE_SINCE_VERSION) {
        wl_seat_release(seat_);
    } else {
        wl_seat_destroy(seat_);
    }
}

wl_pointer* Seat::pointer() const noexcept { return pointer_ != nullptr ? pointer_->pointer() : nullptr; }

wl_surface* Seat::pointer_focus() const noexcept { return pointer_ != nullptr ? pointer_->focus() : nullptr; }

wl_surface* Seat::keyboard_focus() const noexcept { return keyboard_ != nullptr ? keyboard_->focus() : nullptr; }

bool Seat::control_alt() const { return keyboard_ != nullptr && keyboard_->control_alt(); }

void Seat::forget(wl_surface* surface) noexcept {
    if (pointer_ != nullptr) pointer_->forget(surface);
    if (keyboard_ != nullptr) keyboard_->forget(surface);
    if (last_press_.surface == surface) last_press_ = InputSerial{};
}

void Seat::pressed(std::uint32_t serial, wl_surface* surface) { last_press_ = InputSerial{serial, surface, now_ns()}; }

void Seat::capable(void* self, wl_seat* seat, std::uint32_t capabilities) {
    auto* owner = static_cast<Seat*>(self);
    const auto on_press = [owner](std::uint32_t serial, wl_surface* surface) { owner->pressed(serial, surface); };
    const bool pointing = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (pointing && owner->pointer_ == nullptr) {
        owner->pointer_ = std::make_unique<PointerEvents>(wl_seat_get_pointer(seat), owner->version_, owner->cursor_,
                                                          on_press);
        owner->cursor_.use(owner->pointer_->pointer());
        owner->pointer_changed();
    } else if (!pointing && owner->pointer_ != nullptr) {
        owner->cursor_.use(nullptr);
        owner->pointer_.reset();
        owner->pointer_changed();
    }
    const bool typing = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (typing && owner->keyboard_ == nullptr) {
        owner->keyboard_ = std::make_unique<KeyboardEvents>(owner->loop_, wl_seat_get_keyboard(seat),
                                                            owner->version_, on_press,
                                                            [owner] { owner->keyboard_focus_changed(); });
    } else if (!typing && owner->keyboard_ != nullptr) {
        owner->keyboard_.reset();
    }
    RWIN_INFO("seat", "pointer {}, keyboard {}", pointing ? "yes" : "no", typing ? "yes" : "no");
}

void Seat::named(void* , wl_seat* , const char* ) {}

}
