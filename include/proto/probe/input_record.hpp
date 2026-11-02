#pragma once

#include <cstdint>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct InputRecord {
    std::uint32_t message = 0;
    std::uint64_t window = 0;
    std::uint64_t wparam = 0;
    std::uint64_t lparam = 0;
    std::int64_t outcome = 0;

    [[nodiscard]] std::int32_t x() const noexcept {
        return static_cast<std::int16_t>(lparam & 0xFFFF);
    }
    [[nodiscard]] std::int32_t y() const noexcept {
        return static_cast<std::int16_t>((lparam >> 16) & 0xFFFF);
    }

    [[nodiscard]] std::int32_t raw_dx() const noexcept {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(lparam & 0xFFFFFFFF));
    }
    [[nodiscard]] std::int32_t raw_dy() const noexcept {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(lparam >> 32));
    }
    [[nodiscard]] bool raw_absolute() const noexcept { return (wparam & 1) != 0; }

    [[nodiscard]] bool operator==(const InputRecord&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<InputRecord> read(MessageReader& in);
};

}
