#pragma once

#include "proto/probe/screen_pixels.hpp"

#include <cstdint>
#include <optional>

namespace rwin::probe {
[[nodiscard]] std::optional<ScreenPixels> capture_screen(std::int32_t left, std::int32_t top,
                                                         std::uint32_t width,
                                                         std::uint32_t height);

}
