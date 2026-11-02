#pragma once

#include <cstdint>

namespace rwin {
enum class MoveSizeKind : std::uint8_t {
    Left = 1,
    Right = 2,
    Top = 3,
    TopLeft = 4,
    TopRight = 5,
    Bottom = 6,
    BottomLeft = 7,
    BottomRight = 8,
    Move = 9,
    KeyMove = 10,
    KeySize = 11,
};

[[nodiscard]] constexpr bool valid_move_size_kind(std::uint8_t kind) noexcept {
    return kind >= static_cast<std::uint8_t>(MoveSizeKind::Left) && kind <= static_cast<std::uint8_t>(MoveSizeKind::KeySize);
}

}
