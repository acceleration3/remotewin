#include "probe/backdrop_window.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::probe {

BackdropWindow::BackdropWindow(std::uint32_t rgb) {
    const COLORREF colour = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
    brush_ = ::CreateSolidBrush(colour);
    WNDCLASSEXW described{};
    described.cbSize = sizeof(described);
    described.lpfnWndProc = &::DefWindowProcW;
    described.hInstance = ::GetModuleHandleW(nullptr);
    described.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    described.hbrBackground = static_cast<HBRUSH>(brush_);
    described.lpszClassName = kClassName;
    if (::RegisterClassExW(&described) == 0) {
        RWIN_ERROR("backdrop", "RegisterClassEx failed: error {}", ::GetLastError());
        return;
    }
    window_ = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"", WS_POPUP,
                                ::GetSystemMetrics(SM_XVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_YVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
                                ::GetSystemMetrics(SM_CYVIRTUALSCREEN), nullptr, nullptr,
                                ::GetModuleHandleW(nullptr), nullptr);
    if (window_ == nullptr) {
        RWIN_ERROR("backdrop", "CreateWindowEx failed: error {}", ::GetLastError());
        return;
    }
    ::ShowWindow(window_, SW_SHOWNOACTIVATE);
    ::UpdateWindow(window_);
    RWIN_INFO("backdrop", "up, colour {:06x}", rgb);
}

BackdropWindow::~BackdropWindow() {
    if (window_ != nullptr) ::DestroyWindow(window_);
    if (brush_ != nullptr) ::DeleteObject(static_cast<HBRUSH>(brush_));
}

void BackdropWindow::stay_while(std::uint32_t parent_process_id) const {
    HANDLE parent = ::OpenProcess(SYNCHRONIZE, FALSE, parent_process_id);
    if (parent == nullptr) {
        RWIN_ERROR("backdrop", "cannot watch parent {}: error {}", parent_process_id,
                   ::GetLastError());
        return;
    }
    for (;;) {
        const DWORD woke = ::MsgWaitForMultipleObjects(1, &parent, FALSE, INFINITE, QS_ALLINPUT);
        if (woke != WAIT_OBJECT_0 + 1) break;
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
    }
    ::CloseHandle(parent);
}

}
