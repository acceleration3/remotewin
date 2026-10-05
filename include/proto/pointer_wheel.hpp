#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A wheel turned over a mirror: where the pointer was, in the window's
// picture (or the monitor's), and by how much, in Windows' wheel units -
// WHEEL_DELTA, 120, a notch. Positive dy turns away from the user, which
// scrolls up; positive dx tilts right.
struct PointerWheel {
    std::uint64_t window = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t dx = 0;
    std::int32_t dy = 0;

    [[nodiscard]] bool operator==(const PointerWheel&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<PointerWheel> read(MessageReader& in);
};

}  // namespace rwin
