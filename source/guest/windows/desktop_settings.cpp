#include "guest/windows/desktop_settings.hpp"

#include "guest/capture/frame_signal.hpp"
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

Rect read_work_area() {
    RECT area{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0) == FALSE) {
        area.right = ::GetSystemMetrics(SM_CXSCREEN);
        area.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    }
    return Rect{area.left, area.top, static_cast<std::uint32_t>(area.right - area.left),
                static_cast<std::uint32_t>(area.bottom - area.top)};
}

Rect read_monitor() {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor) == FALSE) {
        return Rect{0, 0, static_cast<std::uint32_t>(::GetSystemMetrics(SM_CXSCREEN)),
                    static_cast<std::uint32_t>(::GetSystemMetrics(SM_CYSCREEN))};
    }
    const RECT& area = monitor.rcMonitor;
    return Rect{area.left, area.top, static_cast<std::uint32_t>(area.right - area.left),
                static_cast<std::uint32_t>(area.bottom - area.top)};
}

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SETTINGCHANGE || message == WM_DISPLAYCHANGE) {
        if (auto* settings = reinterpret_cast<DesktopSettings*>(::GetWindowLongPtrW(window, GWLP_USERDATA))) {
            settings->reread();
        }
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

}  // namespace

DesktopSettings::DesktopSettings(const FrameSignal& changes)
    : changes_(changes), drop_shadows_(read_drop_shadows()), work_area_(read_work_area()), monitor_(read_monitor()) {
    worker_ = std::thread([this] { listen(); });
}

DesktopSettings::~DesktopSettings() {
    if (const unsigned long id = thread_id_.load(); id != 0) ::PostThreadMessageW(id, WM_QUIT, 0, 0);
    if (worker_.joinable()) worker_.join();
}

Rect DesktopSettings::work_area() const {
    const std::lock_guard held{lock_};
    return work_area_;
}

Rect DesktopSettings::monitor() const {
    const std::lock_guard held{lock_};
    return monitor_;
}

void DesktopSettings::reread() {
    const bool shadows = read_drop_shadows();
    const Rect area = read_work_area();
    const Rect whole = read_monitor();
    bool moved = drop_shadows_.exchange(shadows) != shadows;
    {
        const std::lock_guard held{lock_};
        moved = moved || !(work_area_ == area) || !(monitor_ == whole);
        work_area_ = area;
        monitor_ = whole;
    }
    if (!moved) return;
    RWIN_INFO("settings", "shadows under windows {}, work area {},{} {}x{}", shadows ? "on" : "off", area.x, area.y,
              area.width, area.height);
    changed_.store(true);
    changes_.raise();
}

void DesktopSettings::listen() {
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
    HWND window = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                    ::GetModuleHandleW(nullptr), nullptr);
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
