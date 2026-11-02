#include "proto/probe/input_record.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

namespace rwin {

void InputRecord::write(MessageWriter& out) const {
    out.put_u32(message);
    out.put_u64(window);
    out.put_u64(wparam);
    out.put_u64(lparam);
    out.put_u64(static_cast<std::uint64_t>(outcome));
}

std::optional<InputRecord> InputRecord::read(MessageReader& in) {
    InputRecord record;
    record.message = in.u32();
    record.window = in.u64();
    record.wparam = in.u64();
    record.lparam = in.u64();
    record.outcome = static_cast<std::int64_t>(in.u64());
    if (!in.ok()) return std::nullopt;
    return record;
}

}
