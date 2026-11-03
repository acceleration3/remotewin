#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rwin::guest {
class InputBatch {
public:
    static constexpr std::size_t kMaxRecords = 8;

    struct Record {
        enum class Kind : std::uint32_t { Mouse = 1, Keyboard = 2 };

        Kind kind = Kind::Mouse;
        std::int32_t dx = 0;
        std::int32_t dy = 0;
        std::uint32_t mouse_data = 0;
        std::uint32_t flags = 0;
        std::uint16_t virtual_key = 0;
        std::uint16_t scan_code = 0;
    };

    bool add_mouse(std::int32_t dx, std::int32_t dy, std::uint32_t mouse_data, std::uint32_t flags);
    bool add_key(std::uint16_t virtual_key, std::uint16_t scan_code, std::uint32_t flags);

    [[nodiscard]] std::span<const Record> records() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return wire_.count == 0; }

    [[nodiscard]] std::span<const std::byte> frame() const noexcept;
    [[nodiscard]] static std::optional<InputBatch> from_frame(std::span<const std::byte> bytes);

    static constexpr std::size_t kMaxFrameBytes = 8 + kMaxRecords * sizeof(Record);

private:
    static constexpr std::uint32_t kMagic = 0x4E495752;

    struct Wire {
        std::uint32_t magic = kMagic;
        std::uint32_t count = 0;
        std::array<Record, kMaxRecords> records{};
    };

    Wire wire_;
};

}
