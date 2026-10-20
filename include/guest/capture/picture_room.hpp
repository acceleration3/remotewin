#pragma once

#include "guest/capture/capture_stream.hpp"

namespace rwin::guest {

// How large the buffers a window's pictures go into - its capture pool, its
// ring of shared textures - are made: larger than the picture, so that a
// window being resized fills the same buffers at each step rather than
// having them made again, which is a new capture session and a new ring
// for the host to import, tens of milliseconds each. A picture is drawn at
// a buffer's top left and its size said with it; the rest goes unused.
//
// Room is the picture rounded up to kStep, and a kStep more; room holds a
// picture that fits in it and leaves no more than kSlack of either side
// unused, so a window that shrank a long way is given smaller buffers.
inline constexpr std::uint32_t kRoomStep = 128;
inline constexpr std::uint32_t kRoomSlack = 4 * kRoomStep;
// Direct3D 11's largest texture side.
inline constexpr std::uint32_t kLargestRoom = 16384;

[[nodiscard]] CaptureStream::Extent room_for(CaptureStream::Extent picture) noexcept;
[[nodiscard]] bool room_holds(CaptureStream::Extent room, CaptureStream::Extent picture) noexcept;

}  // namespace rwin::guest
