#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace rwin::guest {
struct MonochromePicture {
    std::vector<std::uint8_t> bgra;
    bool inverts = false;
};

[[nodiscard]] MonochromePicture monochrome_picture(std::span<const std::uint8_t> planes, std::uint32_t width,
                                                   std::uint32_t height);

}
