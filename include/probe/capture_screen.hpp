#pragma once

#include "proto/probe/screen_pixels.hpp"

#include <cstdint>
#include <optional>

namespace rwin::probe {

// The screen inside a rectangle, as DWM composed it.
//
// Read from the screen's device context with CAPTUREBLT, which is what
// includes layered windows: without it a translucent or colour-keyed
// window is left out, and what is behind it is read instead. Asked after a
// DwmFlush, so the composition read is one that has every paint up to now
// in it. The pointer is not in it; the screen's DC never has the cursor.
[[nodiscard]] std::optional<ScreenPixels> capture_screen(std::int32_t left, std::int32_t top,
                                                         std::uint32_t width,
                                                         std::uint32_t height);

}  // namespace rwin::probe
