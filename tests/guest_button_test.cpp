#include "host/input/guest_button.hpp"
#include "proto/pointer_button.hpp"

#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

bool is(Qt::MouseButton button, rwin::PointerButton::Button guest) {
    return rwin::host::guest_button(button) == static_cast<std::uint8_t>(guest);
}

void every_button_is_its_own() {
    using Button = rwin::PointerButton::Button;
    check(is(Qt::LeftButton, Button::Left), "left is left");
    check(is(Qt::MiddleButton, Button::Middle), "middle is middle");
    check(is(Qt::RightButton, Button::Right), "right is right");
    check(is(Qt::BackButton, Button::Back), "back is XBUTTON1, not a left click");
    check(is(Qt::ForwardButton, Button::Forward), "forward is XBUTTON2, not a left click");
    check(is(Qt::XButton1, Button::Back) && is(Qt::XButton2, Button::Forward), "X buttons are back and forward");
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
