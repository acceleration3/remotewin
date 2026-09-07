#pragma once

#include <cstdint>

// Big-endian integer codecs for the host<->agent link.
//
// The wire is big-endian because that is still the network-byte-order
// convention and it makes packet dumps readable. These used to exist
// twice — once in the agent's transport, once in the manager's link —
// which is exactly the kind of pair that drifts.

namespace rwin::proto {

inline void write_u32_be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFFU);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFFU);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFFU);
    p[3] = static_cast<std::uint8_t>(v & 0xFFU);
}

[[nodiscard]] inline std::uint32_t read_u32_be(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24)
         | (static_cast<std::uint32_t>(p[1]) << 16)
         | (static_cast<std::uint32_t>(p[2]) << 8)
         | static_cast<std::uint32_t>(p[3]);
}

inline void write_u64_be(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<std::uint8_t>((v >> (56 - (8 * i))) & 0xFFU);
    }
}

[[nodiscard]] inline std::uint64_t read_u64_be(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v = (v << 8) | static_cast<std::uint64_t>(p[i]);
    }
    return v;
}

}  // namespace rwin::proto
