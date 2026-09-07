#pragma once

#include "rwin/proto/byte_order.hpp"

#include <cstdint>

// The agent<->manager envelope. Normative description: docs/link-protocol.md.
//
//   [type:u8][len:u32 big-endian][payload:len bytes]
//
// Length-prefixing (rather than newline framing) is what lets binary
// payloads share the connection with JSON without escaping.

namespace rwin::proto {

enum class FrameType : std::uint8_t {
    // JSON control message: request, reply or event.
    Control = 1,
    // Retired. Window pixels used to ride the socket as PNG; they now go
    // through the IVSHMEM frame ring (see frame_ring.hpp). The id stays
    // burnt so an old peer's frames are never mistaken for something else.
    RetiredFrame = 2,
    // Window vanished mid-stream: payload is [hwnd:u64-BE].
    FrameGone = 3,
};

inline constexpr std::size_t kHeaderSize = 1 + 4;  // type + length

// Payload length of a FrameGone envelope.
inline constexpr std::size_t kFrameGonePayloadSize = 8;

// Write the 5-byte header into ``out``, which must have room for
// ``kHeaderSize`` bytes.
inline void write_header(std::uint8_t* out, FrameType type,
                         std::uint32_t payload_len) noexcept {
    out[0] = static_cast<std::uint8_t>(type);
    write_u32_be(out + 1, payload_len);
}

[[nodiscard]] inline std::uint32_t read_payload_len(const std::uint8_t* header) noexcept {
    return read_u32_be(header + 1);
}

[[nodiscard]] inline FrameType read_type(const std::uint8_t* header) noexcept {
    return static_cast<FrameType>(header[0]);
}

}  // namespace rwin::proto
