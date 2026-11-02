#include "guest/capture/capture_clock.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

namespace rwin::guest {

namespace {

constexpr std::int64_t kTicksPerSecond = 10'000'000;

std::int64_t in_ticks(std::int64_t count) noexcept {
    LARGE_INTEGER frequency{};
    ::QueryPerformanceFrequency(&frequency);
    const std::int64_t per_second = frequency.QuadPart;
    return count / per_second * kTicksPerSecond + count % per_second * kTicksPerSecond / per_second;
}

}

std::int64_t capture_clock_now() noexcept {
    LARGE_INTEGER count{};
    ::QueryPerformanceCounter(&count);
    return in_ticks(count.QuadPart);
}

std::int64_t composition_period() noexcept {
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof timing;
    if (FAILED(::DwmGetCompositionTimingInfo(nullptr, &timing))) return 0;
    return in_ticks(static_cast<std::int64_t>(timing.qpcRefreshPeriod));
}

}
