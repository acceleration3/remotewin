#include "host/input/guest_button.hpp"

namespace rwin::host {

std::uint8_t guest_button(Qt::MouseButton button) noexcept {
    if (button == Qt::MiddleButton) return 1;
    if (button == Qt::RightButton) return 2;
    return 0;
}

}  // namespace rwin::host
