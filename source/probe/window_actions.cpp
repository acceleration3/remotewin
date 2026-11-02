#include "probe/window_actions.hpp"

#include "logging/logger.hpp"
#include "probe/dialog_boxes.hpp"
#include "probe/pointer_actions.hpp"
#include "probe/probe_windows.hpp"
#include "proto/probe/probe_action.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <format>

namespace rwin::probe {

namespace {

constexpr const wchar_t* kMessageBoxText = L"probe";

HWND as_window(std::uint64_t handle) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
}

HWND insert_after(ProbeAction::ZOrder z) {
    switch (z) {
        case ProbeAction::ZOrder::Top:
            return HWND_TOP;
        case ProbeAction::ZOrder::Bottom:
            return HWND_BOTTOM;
        case ProbeAction::ZOrder::TopMost:
            return HWND_TOPMOST;
        case ProbeAction::ZOrder::NoTopMost:
            return HWND_NOTOPMOST;
        case ProbeAction::ZOrder::Keep:
            break;
    }
    return nullptr;
}

std::wstring wide(const std::string& text) {
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), length);
    out.resize(static_cast<std::size_t>(length > 0 ? length - 1 : 0));
    return out;
}

std::string refused(const char* call, const ProbeAction& action) {
    return std::format("{} on {:#x} failed: error {}", call, action.window, ::GetLastError());
}

}

WindowActions::WindowActions(ProbeWindows& windows, DialogBoxes& dialogs, PointerActions& pointer)
    : windows_(windows), dialogs_(dialogs), pointer_(pointer) {}

std::optional<std::uint64_t> WindowActions::perform(const ProbeAction& action, std::string& failure) {
    HWND window = as_window(action.window);
    using Kind = ProbeAction::Kind;
    bool done = true;
    switch (action.kind) {
        case Kind::Enable:
            static_cast<void>(::EnableWindow(window, action.on ? TRUE : FALSE));
            break;
        case Kind::Show:
            static_cast<void>(::ShowWindow(window, action.show));
            break;
        case Kind::SetOwner:
            ::SetLastError(0);
            done = ::SetWindowLongPtrW(window, GWLP_HWNDPARENT, static_cast<LONG_PTR>(action.other)) != 0
                   || ::GetLastError() == 0;
            if (!done) failure = refused("SetWindowLongPtr(GWLP_HWNDPARENT)", action);
            break;
        case Kind::Foreground:
            RWIN_INFO("action", "SetForegroundWindow {:#x}: {}", action.window,
                      ::SetForegroundWindow(window) != FALSE);
            break;
        case Kind::SetPosition:
            done = set_position(action, failure);
            break;
        case Kind::Move:
            done = ::MoveWindow(window, action.x, action.y, action.width, action.height, TRUE) != FALSE;
            if (!done) failure = refused("MoveWindow", action);
            break;
        case Kind::OpenModal:
            done = dialogs_.run_modal(action, failure);
            break;
        case Kind::OpenModeless: {
            HWND dialog = dialogs_.open_modeless(action, failure);
            if (dialog == nullptr) return std::nullopt;
            const auto handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(dialog));
            windows_.follow(handle);
            return handle;
        }
        case Kind::EndDialog:
            done = dialogs_.end(window, failure);
            break;
        case Kind::ShowMessageBox:
            done = show_message_box(action, failure);
            break;
        case Kind::CloseMessageBox:
            done = ::PostMessageW(window, WM_CLOSE, 0, 0) != FALSE;
            if (!done) failure = refused("posting WM_CLOSE", action);
            break;
        case Kind::Click:
        case Kind::ClassCursor:
        case Kind::AnswerCursor:
        case Kind::ShowCursor:
        case Kind::ClipCursor:
        case Kind::SetCursorPos:
        case Kind::Capture:
        case Kind::RawInput:
        case Kind::Recentre:
        case Kind::GuestMove:
        case Kind::ActivateShell:
        case Kind::PointerPrecision:
            done = pointer_.perform(action, failure);
            break;
        case Kind::Destroy:
            done = windows_.destroy(action.window);
            if (!done) failure = refused("DestroyWindow", action);
            break;
    }
    if (!done) return std::nullopt;
    return std::uint64_t{0};
}

bool WindowActions::set_position(const ProbeAction& action, std::string& failure) const {
    const UINT flags = action.flags | (action.z == ProbeAction::ZOrder::Keep ? SWP_NOZORDER : 0);
    if (::SetWindowPos(as_window(action.window), insert_after(action.z), action.x, action.y,
                       action.width, action.height, flags) != FALSE) {
        return true;
    }
    failure = refused("SetWindowPos", action);
    return false;
}

bool WindowActions::show_message_box(const ProbeAction& action, std::string& failure) const {
    const std::wstring title = wide(action.title);
    const int pressed = ::MessageBoxW(as_window(action.window), kMessageBoxText, title.c_str(),
                                      MB_OK | action.flags);
    if (pressed != 0) {
        RWIN_INFO("action", "message box '{}' ended with {}", action.title, pressed);
        return true;
    }
    failure = refused("MessageBox", action);
    return false;
}

}
