#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct CursorAt {
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] bool operator==(const CursorAt&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<CursorAt> read(MessageReader& in);
};

}
