#pragma once

#include <cstdint>
#include <optional>

namespace rwin::host {
[[nodiscard]] std::optional<std::uint8_t> guest_button(std::uint32_t code) noexcept;

}
