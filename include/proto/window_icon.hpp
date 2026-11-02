#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowIcon {
    enum class Size : std::uint8_t { Small = 0, Big = 1 };

    static constexpr std::uint16_t kMaxSide = 256;

    WindowId window;
    Size size = Size::Big;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::vector<std::uint8_t> bgra;

    [[nodiscard]] bool operator==(const WindowIcon&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WindowIcon> read(MessageReader& in);
};

}
