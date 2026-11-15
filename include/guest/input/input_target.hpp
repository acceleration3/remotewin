#pragma once

#include <cstdint>

namespace rwin::guest {
class InputTarget {
public:
    void raise(void* window, std::int32_t x, std::int32_t y, bool now);
};

}
