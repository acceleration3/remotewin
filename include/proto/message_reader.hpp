#pragma once

#include "proto/message_id.hpp"

#include <cstdint>
#include <string>

namespace rwin {
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
    std::int64_t i64();
    std::string string();
    const std::uint8_t* bytes(std::size_t size);

private:
    bool want(std::size_t size);

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
    MessageId id_{};
    bool ok_ = true;
};

}
