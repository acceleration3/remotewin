#include "guest/windows/marker_window.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>

namespace rwin::guest {

namespace {

constexpr const wchar_t* kMarkerClass = L"rwin-guest-marker";
constexpr const wchar_t* kKeysClass = L"rwin-guest-hotkey";
constexpr UINT kCarryOut = WM_APP + 1;
// rdpshell's: VK 0xB9, a key no keyboard has, and the next lower one when
// another process holds it, twenty tries at most.
constexpr UINT kFirstHotkey = 0xB9;
constexpr int kHotkeyTries = 20;
constexpr UINT kHotkeyWithinMs = 1000;
constexpr UINT_PTR kHotkeyTimer = 1;

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* marker = reinterpret_cast<MarkerWindow*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (marker != nullptr) {
        switch (message) {
            case kCarryOut:
                marker->carry_out();
                return 0;
            case WM_HOTKEY:
                marker->hotkey_heard();
                return 0;
            case WM_TIMER:
                if (wparam == kHotkeyTimer) marker->timed_out();
                return 0;
            default:
                break;
        }
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

bool registered(const wchar_t* name) {
    WNDCLASSEXW described{};
    described.cbSize = sizeof(described);
    described.lpfnWndProc = &procedure;
    described.hInstance = ::GetModuleHandleW(nullptr);
    described.lpszClassName = name;
    return ::RegisterClassExW(&described) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void send_key(WORD key, bool pressed) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = key;
    input.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(key, MAPVK_VK_TO_VSC));
    input.ki.dwFlags = pressed ? 0 : KEYEVENTF_KEYUP;
    ::SendInput(1, &input, sizeof(input));
}

}  // namespace

MarkerWindow::MarkerWindow() {
    worker_ = std::thread([this] { run(); });
    while (!ready_.load()) std::this_thread::yield();
}

MarkerWindow::~MarkerWindow() {
    if (const unsigned long id = thread_id_.load(); id != 0) ::PostThreadMessageW(id, WM_QUIT, 0, 0);
    if (worker_.joinable()) worker_.join();
}

void MarkerWindow::activate(void* window) {
    wanted_.store(window);
    if (void* marker = marker_.load(); marker != nullptr) ::PostMessageW(static_cast<HWND>(marker), kCarryOut, 0, 0);
}

void MarkerWindow::run() {
    MSG message;
    ::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_.store(::GetCurrentThreadId());
    HWND marker = nullptr;
    HWND keys = nullptr;
    if (registered(kMarkerClass) && registered(kKeysClass)) {
        marker = ::CreateWindowExW(WS_EX_TOOLWINDOW, kMarkerClass, L"remotewin marker", WS_POPUP | WS_VISIBLE, 0, 0,
                                   0, 0, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        keys = ::CreateWindowExW(0, kKeysClass, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                                 ::GetModuleHandleW(nullptr), nullptr);
    }
    if (marker == nullptr || keys == nullptr) {
        RWIN_ERROR("focus", "no marker window (error {}): focus leaving every mirror leaves the guest's foreground "
                   "where it was", ::GetLastError());
    } else {
        ::SetWindowLongPtrW(marker, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ::SetWindowLongPtrW(keys, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ::SetPropW(marker, L"UIA_WindowVisibilityOverridden", reinterpret_cast<HANDLE>(2));
        keys_.store(keys);
        register_hotkey();
        marker_.store(marker);
    }
    ready_.store(true);
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    if (hotkey_id_ != 0) {
        ::UnregisterHotKey(keys, hotkey_id_);
        ::GlobalDeleteAtom(static_cast<ATOM>(hotkey_id_));
    }
    if (keys != nullptr) ::DestroyWindow(keys);
    if (marker != nullptr) ::DestroyWindow(marker);
}

void MarkerWindow::register_hotkey() {
    const ATOM id = ::GlobalAddAtomW(L"rwin-guest-hotkey");
    for (int attempt = 0; attempt < kHotkeyTries; ++attempt) {
        const UINT key = kFirstHotkey - static_cast<UINT>(attempt);
        if (::RegisterHotKey(static_cast<HWND>(keys_.load()), id, 0, key) != FALSE) {
            hotkey_id_ = id;
            hotkey_key_ = key;
            RWIN_DEBUG("focus", "foreground hotkey is VK {:#x}", key);
            return;
        }
        if (::GetLastError() == ERROR_ACCESS_DENIED) break;
    }
    ::GlobalDeleteAtom(id);
    RWIN_WARN("focus", "no foreground hotkey (error {}): the guest may refuse to bring a window forward",
              ::GetLastError());
}

void MarkerWindow::carry_out() {
    // The hotkey's wait pumps this thread's messages, which may carry a
    // newer request out from inside an older one.
    if (carrying_out_) return;
    carrying_out_ = true;
    for (void* window = wanted_.exchange(nullptr); window != nullptr; window = wanted_.exchange(nullptr)) {
        const auto target = static_cast<HWND>(window);
        if (::IsWindow(target) == FALSE) continue;
        const bool taken = (::SetForegroundWindow(target) != FALSE && ::GetForegroundWindow() == target)
                           || take_with_hotkey(window);
        RWIN_DEBUG("focus", "{:#x} {} the foreground", reinterpret_cast<std::uintptr_t>(window),
                   taken ? "has" : "was refused");
    }
    carrying_out_ = false;
}

bool MarkerWindow::take_with_hotkey(void* window) {
    if (hotkey_id_ == 0) return false;
    const auto keys = static_cast<HWND>(keys_.load());
    constexpr std::array<WORD, 3> kModifiers{VK_SHIFT, VK_MENU, VK_CONTROL};
    std::array<bool, kModifiers.size()> held{};
    for (std::size_t i = 0; i < kModifiers.size(); ++i) {
        held[i] = (::GetAsyncKeyState(kModifiers[i]) & 0x8000) != 0;
        if (held[i]) send_key(kModifiers[i], false);
    }
    send_key(static_cast<WORD>(hotkey_key_), true);
    send_key(static_cast<WORD>(hotkey_key_), false);
    for (std::size_t i = 0; i < kModifiers.size(); ++i) {
        if (held[i]) send_key(kModifiers[i], true);
    }
    hotkey_heard_ = false;
    timed_out_ = false;
    ::SetTimer(keys, kHotkeyTimer, kHotkeyWithinMs, nullptr);
    MSG message;
    while (!hotkey_heard_ && !timed_out_) {
        if (::GetMessageW(&message, nullptr, 0, 0) <= 0) {
            // The quit is the outer loop's to see.
            ::PostQuitMessage(0);
            break;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    ::KillTimer(keys, kHotkeyTimer);
    if (!hotkey_heard_) return false;
    return ::SetForegroundWindow(static_cast<HWND>(window)) != FALSE;
}

}  // namespace rwin::guest
