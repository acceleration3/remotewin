#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// The guest starts saying every window afresh (RAIL's Desktop ARC_BEGAN):
// what it names before SyncEnd is everything there is, and a window the
// host knew that it does not name is gone. After the hello, and whenever
// the guest may have missed events (Resync).
struct SyncBegin {
    enum class Reason : std::uint8_t { Connect = 0, Resync = 1 };

    Reason reason = Reason::Connect;

    [[nodiscard]] bool operator==(const SyncBegin&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SyncBegin> read(MessageReader& in);
};

}  // namespace rwin
