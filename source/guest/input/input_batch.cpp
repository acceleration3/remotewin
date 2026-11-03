#include "guest/input/input_batch.hpp"

#include <cstring>
#include <type_traits>

namespace rwin::guest {

namespace {
constexpr std::size_t kHeaderBytes = 8;
}

static_assert(std::is_trivially_copyable_v<InputBatch::Record>);
static_assert(sizeof(InputBatch::Record) == 24);

bool InputBatch::add_mouse(std::int32_t dx, std::int32_t dy, std::uint32_t mouse_data, std::uint32_t flags) {
    if (wire_.count >= kMaxRecords) return false;
    Record& record = wire_.records[wire_.count++];
    record = Record{Record::Kind::Mouse, dx, dy, mouse_data, flags, 0, 0};
    return true;
}

bool InputBatch::add_key(std::uint16_t virtual_key, std::uint16_t scan_code, std::uint32_t flags) {
    if (wire_.count >= kMaxRecords) return false;
    Record& record = wire_.records[wire_.count++];
    record = Record{Record::Kind::Keyboard, 0, 0, 0, flags, virtual_key, scan_code};
    return true;
}

std::span<const InputBatch::Record> InputBatch::records() const noexcept {
    return std::span<const Record>{wire_.records.data(), wire_.count};
}

std::span<const std::byte> InputBatch::frame() const noexcept {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(&wire_),
                                      kHeaderBytes + wire_.count * sizeof(Record)};
}

std::optional<InputBatch> InputBatch::from_frame(std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderBytes) return std::nullopt;
    std::uint32_t magic = 0;
    std::uint32_t count = 0;
    std::memcpy(&magic, bytes.data(), sizeof magic);
    std::memcpy(&count, bytes.data() + sizeof magic, sizeof count);
    if (magic != kMagic || count == 0 || count > kMaxRecords) return std::nullopt;
    if (bytes.size() != kHeaderBytes + count * sizeof(Record)) return std::nullopt;
    InputBatch batch;
    batch.wire_.count = count;
    std::memcpy(batch.wire_.records.data(), bytes.data() + kHeaderBytes, count * sizeof(Record));
    for (const Record& record : batch.records()) {
        if (record.kind != Record::Kind::Mouse && record.kind != Record::Kind::Keyboard) return std::nullopt;
    }
    return batch;
}

}
