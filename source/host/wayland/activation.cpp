#include "host/wayland/activation.hpp"

#include "host/wayland/globals.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"

#include <wayland-client.h>
#include <xdg-activation-v1-client-protocol.h>

#include <algorithm>

namespace rwin::host {

Activation::Activation(const Globals& globals, Seat& seat) : globals_(globals), seat_(seat) {}

Activation::~Activation() {
    for (const Asked& asked : asked_) xdg_activation_token_v1_destroy(asked.token);
}

bool Activation::offered() const noexcept { return globals_.activation() != nullptr; }

void Activation::activate(wl_surface* surface, const std::string& token) {
    if (!offered() || surface == nullptr || token.empty()) return;
    xdg_activation_v1_activate(globals_.activation(), token.c_str(), surface);
    RWIN_DEBUG("focus", "activation asked with a token");
}

void Activation::hand_to(wl_surface* surface) {
    if (!offered() || surface == nullptr) return;
    xdg_activation_token_v1* token = xdg_activation_v1_get_activation_token(globals_.activation());
    static constexpr xdg_activation_token_v1_listener kListener{&Activation::done};
    xdg_activation_token_v1_add_listener(token, &kListener, this);
    const InputSerial& press = seat_.last_press();
    if (press.valid()) xdg_activation_token_v1_set_serial(token, press.serial, seat_.seat());
    if (wl_surface* focused = seat_.keyboard_focus()) xdg_activation_token_v1_set_surface(token, focused);
    xdg_activation_token_v1_commit(token);
    asked_.push_back(Asked{token, surface});
}

void Activation::forget(wl_surface* surface) noexcept {
    for (Asked& asked : asked_) {
        if (asked.target == surface) asked.target = nullptr;
    }
}

void Activation::done(void* self, xdg_activation_token_v1* token, const char* name) {
    auto* activation = static_cast<Activation*>(self);
    const auto asked = std::find_if(activation->asked_.begin(), activation->asked_.end(),
                                    [token](const Asked& known) { return known.token == token; });
    if (asked == activation->asked_.end()) return;
    wl_surface* target = asked->target;
    activation->asked_.erase(asked);
    xdg_activation_token_v1_destroy(token);
    if (target != nullptr) activation->activate(target, name);
}

}
