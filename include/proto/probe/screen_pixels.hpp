#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct ScreenPixels {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> bgra;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ScreenPixels> read(MessageReader& in);
};

}
