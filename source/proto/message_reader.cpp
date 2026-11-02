#include "proto/message_reader.hpp"

namespace rwin {

MessageReader::MessageReader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size) {
    id_ = static_cast<MessageId>(u8());
}

bool MessageReader::want(std::size_t size) {
    if (!ok_ || size_ - at_ < size) {
        ok_ = false;
        return false;
    }
    return true;
}

std::uint8_t MessageReader::u8() {
    if (!want(1)) return 0;
    return data_[at_++];
}

std::uint16_t MessageReader::u16() {
    const auto hi = static_cast<std::uint16_t>(u8());
    return static_cast<std::uint16_t>((hi << 8) | u8());
}

std::uint32_t MessageReader::u32() {
    const auto hi = static_cast<std::uint32_t>(u16());
    return (hi << 16) | u16();
}

std::uint64_t MessageReader::u64() {
    const auto hi = static_cast<std::uint64_t>(u32());
    return (hi << 32) | u32();
}

std::int32_t MessageReader::i32() { return static_cast<std::int32_t>(u32()); }

std::int64_t MessageReader::i64() { return static_cast<std::int64_t>(u64()); }

std::string MessageReader::string() {
    const std::size_t size = u32();
    if (!want(size)) return {};
    const auto* from = reinterpret_cast<const char*>(data_ + at_);
    at_ += size;
    return std::string(from, size);
}

const std::uint8_t* MessageReader::bytes(std::size_t size) {
    if (!want(size)) return nullptr;
    const std::uint8_t* from = data_ + at_;
    at_ += size;
    return from;
}

}
