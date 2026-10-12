#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The guest's answer to a ClockProbe (ClockEcho, guest to host): the host's
// moment the probe carried, sent back unchanged, and the guest's own clock
// when it answered - its performance counter in 100 ns units, the clock its
// frames are stamped with (FrameInSlot).
struct ClockEcho {
    std::uint64_t probed = 0;
    std::int64_t guest = 0;

    [[nodiscard]] bool operator==(const ClockEcho&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<ClockEcho> read(MessageReader& in);
};

}  // namespace rwin
