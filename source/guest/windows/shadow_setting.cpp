#include "guest/windows/shadow_setting.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

constexpr const wchar_t* kClassName = L"rwin-guest-settings";

bool read_drop_shadows() {
    BOOL shadows = FALSE;
    if (::SystemParametersInfoW(SPI_GETDROPSHADOW, 0, &shadows, 0) == FALSE) {
        RWIN_WARN("settings", "SPI_GETDROPSHADOW failed: error {}", ::GetLastError());
        return false;
    }
    return shadows != FALSE;
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SETTINGCHANGE) {
        if (auto* setting = reinterpret_cast<ShadowSetting*>(
                ::GetWindowLongPtrW(window, GWLP_USERDATA))) {
            setting->reread();
        }
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

ShadowSetting::ShadowSetting() : enabled_(read_drop_shadows()) {
    worker_ = std::thread([this] { listen(); });
}

ShadowSetting::~ShadowSetting() {
    if (const unsigned long id = thread_id_.load(); id != 0) {
        ::PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    if (worker_.joinable()) worker_.join();
}

void ShadowSetting::reread() {
    const bool now = read_drop_shadows();
    if (enabled_.exchange(now) == now) return;
    changed_.store(true);
    RWIN_INFO("settings", "shadows under windows are now {}", now ? "on" : "off");
}

void ShadowSetting::listen() {
    WNDCLASSEXW described{};
    described.cbSize = sizeof(described);
    described.lpfnWndProc = &procedure;
    described.hInstance = ::GetModuleHandleW(nullptr);
    described.lpszClassName = kClassName;
    if (::RegisterClassExW(&described) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        RWIN_WARN("settings", "no window class for settings: error {}", ::GetLastError());
        return;
    }
    // A top-level window, never shown: a message-only window is not sent
    // broadcasts.
    HWND window = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 0, 0,
                                    nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (window == nullptr) {
        RWIN_WARN("settings", "no window for settings changes: error {}", ::GetLastError());
        return;
    }
    ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    thread_id_.store(::GetCurrentThreadId());
    // Read again now that a change can no longer slip past unannounced.
    reread();

    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    ::DestroyWindow(window);
    thread_id_.store(0);
}

}  // namespace rwin::guest
