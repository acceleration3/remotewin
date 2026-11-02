#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct GuestCursor {
    std::uint64_t id = 0;
    bool shown = false;

    [[nodiscard]] bool operator==(const GuestCursor&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<GuestCursor> read(MessageReader& in);
};

}
