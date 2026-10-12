#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The first message each end sends on a mirror connection (HostHello,
// GuestHello): which wire it speaks.
//
// The two halves ship apart - the host's beside whoever spawns it, the
// guest's inside a virtual machine that keeps whatever was put there - and
// a field added on one side and not the other is read by the other as the
// start of the next, with nothing to say so. Checked before anything else
// is read, a mismatch is one line naming both versions instead.
struct WireHello {
    // Raised with every change to what any mirror message carries.
    static constexpr std::uint32_t kVersion = 5;

    std::uint32_t version = kVersion;

    [[nodiscard]] bool operator==(const WireHello&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WireHello> read(MessageReader& in);
};

}  // namespace rwin
