#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwin::host {

// One request to the GPU device's export socket (accel-virt's
// docs/external-export.md 3.1): 32 bytes, little-endian, one datagram.
struct ExportRequest {
    static constexpr std::size_t kBytes = 32;
    static constexpr std::uint32_t kMagic = 0x51585641;  // "AVXQ"
    static constexpr std::uint16_t kMajor = 1;
    static constexpr std::uint16_t kMinor = 0;

    enum class Operation : std::uint32_t {
        // Which GPU the surfaces are on.
        Hello = 1,
        // A surface's memory, as a dma-buf; held by the connection from then.
        Export = 2,
        // How many copies into a surface have run, and a sync_file for them.
        Sync = 3,
        // The hold let go of.
        Release = 4,
    };

    Operation operation = Operation::Hello;
    // The guest's id for the surface; 0 for hello and only for hello.
    std::uint64_t surface = 0;
    // This side's own, answered back.
    std::uint64_t tag = 0;

    [[nodiscard]] std::array<std::uint8_t, kBytes> encode() const noexcept;
};

}  // namespace rwin::host
