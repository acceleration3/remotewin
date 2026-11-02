#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SyncBegin {
    enum class Reason : std::uint8_t { Connect = 0, Resync = 1 };

    Reason reason = Reason::Connect;

    [[nodiscard]] bool operator==(const SyncBegin&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SyncBegin> read(MessageReader& in);
};

}
