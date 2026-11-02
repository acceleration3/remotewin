#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwin::host {
struct ExportRequest {
    static constexpr std::size_t kBytes = 32;
    static constexpr std::uint32_t kMagic = 0x51585641;
    static constexpr std::uint16_t kMajor = 1;
    static constexpr std::uint16_t kMinor = 5;
    static constexpr std::uint32_t kOtherGpu = 1;

    enum class Operation : std::uint32_t {
        Hello = 1,
        Export = 2,
        Sync = 3,
        Release = 4,
        Timing = 5,
    };

    Operation operation = Operation::Hello;
    std::uint64_t surface = 0;
    std::uint64_t tag = 0;
    std::uint32_t flags = 0;
    std::int64_t shift_ns = 0;

    [[nodiscard]] std::array<std::uint8_t, kBytes> encode() const noexcept;
};

}
