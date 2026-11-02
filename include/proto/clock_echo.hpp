#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct ClockEcho {
    std::uint64_t probed = 0;
    std::int64_t guest = 0;

    [[nodiscard]] bool operator==(const ClockEcho&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ClockEcho> read(MessageReader& in);
};

}
