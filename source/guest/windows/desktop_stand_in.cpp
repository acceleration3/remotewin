#include "guest/windows/desktop_stand_in.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

// Where Windows itself keeps a minimised window's frame: on no monitor.
constexpr int kOffScreen = -32000;

}  // namespace

DesktopStandIn::DesktopStandIn() {
    worker_ = std::thread([this] { run(); });
}

DesktopStandIn::~DesktopStandIn() {
    if (const unsigned long id = thread_id_.load(); id != 0) {
        ::PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    if (worker_.joinable()) worker_.join();
}

void DesktopStandIn::run() {
    thread_id_.store(::GetCurrentThreadId());

    WNDCLASSW window_class{};
    window_class.lpfnWndProc = &DefWindowProcW;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"remotewin.DesktopStandIn";
    if (::RegisterClassW(&window_class) == 0) {
        RWIN_WARN("standin", "cannot register the stand-in class (error {})",
                  ::GetLastError());
        thread_id_.store(0);
        return;
    }
    HWND window = ::CreateWindowExW(WS_EX_TOOLWINDOW, window_class.lpszClassName, L"",
                                    WS_POPUP | WS_VISIBLE, kOffScreen, kOffScreen, 1, 1,
                                    nullptr, nullptr, window_class.hInstance, nullptr);
    if (window == nullptr) {
        RWIN_WARN("standin", "cannot make the stand-in window (error {})", ::GetLastError());
        thread_id_.store(0);
        return;
    }
    window_.store(window);

    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    window_.store(nullptr);
    ::DestroyWindow(window);
    thread_id_.store(0);
}

void DesktopStandIn::take_foreground() const {
    auto window = static_cast<HWND>(window_.load());
    if (window == nullptr) return;
    ::AllowSetForegroundWindow(ASFW_ANY);
    if (::SetForegroundWindow(window) == FALSE) {
        RWIN_DEBUG("standin", "the foreground was not given up");
    }
}

}  // namespace rwin::guest
