#pragma once

#include "guest/capture/capture_stream.hpp"

namespace rwin::guest {
inline constexpr std::uint32_t kRoomStep = 128;
inline constexpr std::uint32_t kRoomSlack = 4 * kRoomStep;
inline constexpr std::uint32_t kLargestRoom = 16384;

[[nodiscard]] CaptureStream::Extent room_for(CaptureStream::Extent picture) noexcept;
[[nodiscard]] bool room_holds(CaptureStream::Extent room, CaptureStream::Extent picture) noexcept;

}
