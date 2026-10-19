#include "proto/message_writer.hpp"

#include <cstring>

namespace rwin {

namespace {
constexpr std::size_t kLengthPrefixSize = 4;
}

MessageWriter::MessageWriter(MessageId id) {
    bytes_.resize(kLengthPrefixSize);
    put_u8(static_cast<std::uint8_t>(id));
}

void MessageWriter::put_u8(std::uint8_t v) { bytes_.push_back(v); }

void MessageWriter::put_u16(std::uint16_t v) {
    put_u8(static_cast<std::uint8_t>(v >> 8));
    put_u8(static_cast<std::uint8_t>(v));
}

void MessageWriter::put_u32(std::uint32_t v) {
    put_u16(static_cast<std::uint16_t>(v >> 16));
    put_u16(static_cast<std::uint16_t>(v));
}

void MessageWriter::put_u64(std::uint64_t v) {
    put_u32(static_cast<std::uint32_t>(v >> 32));
    put_u32(static_cast<std::uint32_t>(v));
}

void MessageWriter::put_i32(std::int32_t v) {
    put_u32(static_cast<std::uint32_t>(v));
}

void MessageWriter::put_i64(std::int64_t v) { put_u64(static_cast<std::uint64_t>(v)); }

void MessageWriter::put_string(std::string_view v) {
    put_u32(static_cast<std::uint32_t>(v.size()));
    put_bytes(reinterpret_cast<const std::uint8_t*>(v.data()), v.size());
}

void MessageWriter::put_bytes(const std::uint8_t* data, std::size_t size) {
    bytes_.insert(bytes_.end(), data, data + size);
}

std::vector<std::uint8_t> MessageWriter::take() {
    const auto payload = static_cast<std::uint32_t>(bytes_.size() - kLengthPrefixSize);
    bytes_[0] = static_cast<std::uint8_t>(payload >> 24);
    bytes_[1] = static_cast<std::uint8_t>(payload >> 16);
    bytes_[2] = static_cast<std::uint8_t>(payload >> 8);
    bytes_[3] = static_cast<std::uint8_t>(payload);
    return std::move(bytes_);
}

}  // namespace rwin
