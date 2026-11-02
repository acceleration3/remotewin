#include "proto/sys_command.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include <array>

namespace rwin {

namespace {

constexpr std::array kCommands{SysCommand::Command::Size,     SysCommand::Command::Move,
                               SysCommand::Command::Minimize, SysCommand::Command::Maximize,
                               SysCommand::Command::Close,    SysCommand::Command::KeyMenu,
                               SysCommand::Command::Restore,  SysCommand::Command::Default};

}

void SysCommand::write(MessageWriter& out) const {
    window.write(out);
    out.put_u16(static_cast<std::uint16_t>(command));
}

std::optional<SysCommand> SysCommand::read(MessageReader& in) {
    const std::optional<WindowId> window = WindowId::read(in);
    const std::uint16_t command = in.u16();
    if (!window.has_value() || !in.ok()) return std::nullopt;
    for (const Command known : kCommands) {
        if (static_cast<std::uint16_t>(known) == command) return SysCommand{*window, known};
    }
    return std::nullopt;
}

}
