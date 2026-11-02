#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct PointerGrab {
    enum class Kind : std::uint8_t { None, Confine, Lock };

    struct Area {
        std::int32_t left = 0;
        std::int32_t top = 0;
        std::int32_t right = 0;
        std::int32_t bottom = 0;

        [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
        [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
        [[nodiscard]] bool operator==(const Area&) const noexcept = default;
    };

    Kind kind = Kind::None;
    WindowId window;
    Area area;
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const PointerGrab&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerGrab> read(MessageReader& in);
};

}
