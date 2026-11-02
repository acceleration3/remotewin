#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct PictureRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    [[nodiscard]] bool operator==(const PictureRect&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PictureRect> read(MessageReader& in);
};

}
