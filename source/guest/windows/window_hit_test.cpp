#include "guest/windows/window_hit_test.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {
constexpr UINT kAnswerTimeoutMs = 50;
}

std::int32_t WindowHitTest::at(const WindowSurface& surface, std::int32_t window_x,
                               std::int32_t window_y) const {
    if (!surface.valid()) return HTNOWHERE;
    const std::int32_t x = surface.screen_x(window_x);
    const std::int32_t y = surface.screen_y(window_y);

    DWORD_PTR answer = HTNOWHERE;
    ::SendMessageTimeoutW(static_cast<HWND>(surface.handle()), WM_NCHITTEST, 0,
                          MAKELPARAM(static_cast<WORD>(x), static_cast<WORD>(y)),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kAnswerTimeoutMs, &answer);
    return static_cast<std::int32_t>(answer);
}

}  // namespace rwin::guest
