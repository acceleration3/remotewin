#pragma once

#include <cstdint>

namespace rwin::host {
struct PixelsView {
    const std::uint8_t* bgra = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;
};

}
