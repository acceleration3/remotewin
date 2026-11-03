#pragma once

#include <cstdint>

namespace rwin::host {
[[nodiscard]] std::uint16_t qnum_code(std::uint32_t evdev_code) noexcept;

}
