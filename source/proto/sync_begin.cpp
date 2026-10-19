#include "proto/sync_begin.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void SyncBegin::write(MessageWriter& out) const { out.put_u8(static_cast<std::uint8_t>(reason)); }

std::optional<SyncBegin> SyncBegin::read(MessageReader& in) {
    const std::uint8_t reason = in.u8();
    if (!in.ok() || reason > static_cast<std::uint8_t>(Reason::Resync)) return std::nullopt;
    return SyncBegin{static_cast<Reason>(reason)};
}

}  // namespace rwin
