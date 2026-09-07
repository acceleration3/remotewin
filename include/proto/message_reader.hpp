#pragma once

#include "proto/message_id.hpp"

#include <cstdint>
#include <string>

namespace rwin {

// Reads one message payload. Every getter is bounds-checked and a read
// past the end poisons the reader rather than throwing: the bytes came
// from the other side of a boundary, so a malformed message is data to
// be rejected, not an exception to unwind through.
class MessageReader {
public:
    MessageReader(const std::uint8_t* data, std::size_t size);

    [[nodiscard]] MessageId id() const noexcept { return id_; }
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - at_; }

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    std::int32_t i32();
    std::string string();
    // Points into the caller's buffer; valid only while it lives.
    const std::uint8_t* bytes(std::size_t size);

private:
    bool want(std::size_t size);

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
    MessageId id_{};
    bool ok_ = true;
};

}  // namespace rwin
