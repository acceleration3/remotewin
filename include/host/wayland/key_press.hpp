#pragma once

#include <cstdint>

namespace rwin::host {
struct KeyPress {
    std::uint32_t code = 0;
    bool pressed = false;
    bool repeat = false;
    std::uint32_t keysym = 0;
    std::uint32_t character = 0;
};

}
