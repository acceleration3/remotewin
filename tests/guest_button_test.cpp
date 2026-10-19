#include "host/input/guest_button.hpp"
#include "proto/pointer_button.hpp"

#include <linux/input-event-codes.h>

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

bool is(std::uint32_t code, rwin::PointerButton::Button guest) {
    return rwin::host::guest_button(code) == static_cast<std::uint8_t>(guest);
}

void every_button_is_its_own() {
    using Button = rwin::PointerButton::Button;
    check(is(BTN_LEFT, Button::Left), "left is left");
    check(is(BTN_MIDDLE, Button::Middle), "middle is middle");
    check(is(BTN_RIGHT, Button::Right), "right is right");
    check(is(BTN_SIDE, Button::Back), "the side button is XBUTTON1, not a left click");
    check(is(BTN_EXTRA, Button::Forward), "the extra button is XBUTTON2, not a left click");
    check(is(BTN_BACK, Button::Back) && is(BTN_FORWARD, Button::Forward), "back and forward by name too");
    check(!rwin::host::guest_button(BTN_TASK).has_value(), "a button Windows has no number for is no click");
}

}  // namespace

int main() {
    every_button_is_its_own();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("guest_button: all checks passed\n");
    return EXIT_SUCCESS;
}
