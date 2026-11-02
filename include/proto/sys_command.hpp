#pragma once

#include "proto/window_id.hpp"

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct SysCommand {
    enum class Command : std::uint16_t {
        Size = 0xF000,
        Move = 0xF010,
        Minimize = 0xF020,
        Maximize = 0xF030,
        Close = 0xF060,
        KeyMenu = 0xF100,
        Restore = 0xF120,
        Default = 0xF160,
    };

    WindowId window;
    Command command = Command::Close;

    [[nodiscard]] bool operator==(const SysCommand&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<SysCommand> read(MessageReader& in);
};

}
