#pragma once

#include <cstdint>

namespace rwin::guest {
class InputTarget {
public:
    InputTarget() = default;
    InputTarget(const InputTarget&) = delete;
    InputTarget& operator=(const InputTarget&) = delete;
    InputTarget(InputTarget&&) = delete;
    InputTarget& operator=(InputTarget&&) = delete;
    ~InputTarget();

    void raise(void* window, std::int32_t x, std::int32_t y, bool now);

private:
    void give_back_topmost(void* keep, bool now) noexcept;

    void* lent_topmost_ = nullptr;
};

}
