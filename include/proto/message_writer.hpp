#pragma once

#include "proto/message_id.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace rwin {
class MessageWriter {
public:
    explicit MessageWriter(MessageId id);

    void put_u8(std::uint8_t v);
    void put_u16(std::uint16_t v);
    void put_u32(std::uint32_t v);
    void put_u64(std::uint64_t v);
    void put_i32(std::int32_t v);
    void put_i64(std::int64_t v);
    void put_string(std::string_view v);
    void put_bytes(const std::uint8_t* data, std::size_t size);

    [[nodiscard]] std::vector<std::uint8_t> take();

private:
    std::vector<std::uint8_t> bytes_;
};

}
