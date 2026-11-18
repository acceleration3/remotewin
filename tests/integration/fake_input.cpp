#include "fake_input.hpp"

#include <fake-input-client-protocol.h>
#include <wayland-client.h>

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace rwin::integration {

namespace {
constexpr std::uint32_t kKeyboard = 4;
constexpr std::uint32_t kPressed = 1;
constexpr std::uint32_t kReleased = 0;

}

FakeInput::FakeInput(const std::string& socket) : display_(wl_display_connect(socket.c_str())) {
    if (display_ == nullptr) throw std::runtime_error("cannot connect to the compositor at " + socket);
    registry_ = wl_display_get_registry(display_);
    static constexpr wl_registry_listener kListener{&FakeInput::announced, &FakeInput::withdrawn};
    wl_registry_add_listener(registry_, &kListener, this);
    wl_display_roundtrip(display_);
    if (input_ == nullptr) return;
    org_kde_kwin_fake_input_authenticate(input_, "remotewin integration", "drives the pointer and keyboard of a test compositor");
    settle();
}

FakeInput::~FakeInput() {
    if (input_ != nullptr) org_kde_kwin_fake_input_destroy(input_);
    if (registry_ != nullptr) wl_registry_destroy(registry_);
    wl_display_disconnect(display_);
}

void FakeInput::announced(void* self, wl_registry* registry, std::uint32_t name, const char* interface,
                            std::uint32_t version) {
    if (std::strcmp(interface, org_kde_kwin_fake_input_interface.name) != 0 || version < kKeyboard) return;
    auto* fake = static_cast<FakeInput*>(self);
    const std::uint32_t bound = std::min<std::uint32_t>(version, org_kde_kwin_fake_input_interface.version);
    fake->input_ = static_cast<org_kde_kwin_fake_input*>(
        wl_registry_bind(registry, name, &org_kde_kwin_fake_input_interface, bound));
}

void FakeInput::withdrawn(void* , wl_registry* , std::uint32_t ) {}

void FakeInput::move_to(double x, double y) {
    org_kde_kwin_fake_input_pointer_motion_absolute(input_, wl_fixed_from_double(x), wl_fixed_from_double(y));
    settle();
}

void FakeInput::press_button() {
    org_kde_kwin_fake_input_button(input_, BTN_LEFT, kPressed);
    settle();
}

void FakeInput::release_button() {
    org_kde_kwin_fake_input_button(input_, BTN_LEFT, kReleased);
    settle();
}

void FakeInput::press_key(std::uint32_t evdev_code) {
    org_kde_kwin_fake_input_keyboard_key(input_, evdev_code, kPressed);
    settle();
}

void FakeInput::release_key(std::uint32_t evdev_code) {
    org_kde_kwin_fake_input_keyboard_key(input_, evdev_code, kReleased);
    settle();
}

void FakeInput::settle() { wl_display_roundtrip(display_); }

}
