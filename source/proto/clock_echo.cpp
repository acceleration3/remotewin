#include "proto/clock_echo.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void ClockEcho::write(MessageWriter& out) const {
    out.put_u64(probed);
    out.put_u64(static_cast<std::uint64_t>(guest));
}

std::optional<ClockEcho> ClockEcho::read(MessageReader& in) {
    ClockEcho echo;
    echo.probed = in.u64();
    echo.guest = static_cast<std::int64_t>(in.u64());
    if (!in.ok()) return std::nullopt;
    return echo;
}

}
