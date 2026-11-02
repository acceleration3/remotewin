#pragma once

#include <cstdint>

namespace rwin::guest {
[[nodiscard]] std::int64_t capture_clock_now() noexcept;

[[nodiscard]] std::int64_t composition_period() noexcept;

}
