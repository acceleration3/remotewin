#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct Rect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool operator==(const Rect&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<Rect> read(MessageReader& in);
};

}
