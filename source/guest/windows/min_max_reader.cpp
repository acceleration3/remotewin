#include "guest/windows/min_max_reader.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

constexpr UINT kAnswerWithinMs = 1000;

}

MinMax read_min_max(void* window, WindowId id) {
    MINMAXINFO info{};
    info.ptMaxSize = {::GetSystemMetrics(SM_CXMAXIMIZED), ::GetSystemMetrics(SM_CYMAXIMIZED)};
    info.ptMinTrackSize = {::GetSystemMetrics(SM_CXMINTRACK), ::GetSystemMetrics(SM_CYMINTRACK)};
    info.ptMaxTrackSize = {::GetSystemMetrics(SM_CXMAXTRACK), ::GetSystemMetrics(SM_CYMAXTRACK)};
    DWORD_PTR answer = 0;
    ::SendMessageTimeoutW(static_cast<HWND>(window), WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&info),
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, kAnswerWithinMs, &answer);
    return MinMax{id,
                  info.ptMaxSize.x,
                  info.ptMaxSize.y,
                  info.ptMaxPosition.x,
                  info.ptMaxPosition.y,
                  info.ptMinTrackSize.x,
                  info.ptMinTrackSize.y,
                  info.ptMaxTrackSize.x,
                  info.ptMaxTrackSize.y};
}

}
