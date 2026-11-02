#include "probe/pointer_actions.hpp"

#include "logging/logger.hpp"
#include "probe/probe_cursors.hpp"
#include "probe/probe_windows.hpp"
#include "proto/probe/probe_action.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>
#include <format>
#include <optional>

namespace rwin::probe {

namespace {
constexpr long long kAbsoluteRange = 65535;
constexpr USHORT kGenericDesktop = 1;
constexpr USHORT kMouseUsage = 2;

HWND as_window(std::uint64_t handle) { return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle)); }

std::string refused(const char* call, const ProbeAction& action) {
    return std::format("{} on {:#x} failed: error {}", call, action.window, ::GetLastError());
}

POINT on_screen(const ProbeAction& action) {
    POINT at{action.x, action.y};
    ::ClientToScreen(as_window(action.window), &at);
    return at;
}

INPUT absolute_move(POINT at) {
    const int width = ::GetSystemMetrics(SM_CXSCREEN);
    const int height = ::GetSystemMetrics(SM_CYSCREEN);
    INPUT input{};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input.mi.dx = static_cast<LONG>(at.x * kAbsoluteRange / (width - 1));
    input.mi.dy = static_cast<LONG>(at.y * kAbsoluteRange / (height - 1));
    return input;
}

}

PointerActions::PointerActions(ProbeWindows& windows, const ProbeCursors& cursors)
    : windows_(windows), cursors_(cursors) {
    static_cast<void>(::ShowCursor(TRUE));
    display_count_ = ::ShowCursor(FALSE);
    resting_count_ = display_count_;
}

bool PointerActions::perform(const ProbeAction& action, std::string& failure) {
    using Kind = ProbeAction::Kind;
    switch (action.kind) {
        case Kind::ClassCursor:
            return set_class_cursor(action, failure);
        case Kind::AnswerCursor:
            return answer_cursor(action, failure);
        case Kind::ShowCursor:
            show_cursor(action.on);
            return true;
        case Kind::ClipCursor:
            return clip(action, failure);
        case Kind::SetCursorPos: {
            const POINT at = on_screen(action);
            if (::SetCursorPos(at.x, at.y) != FALSE) return true;
            failure = refused("SetCursorPos", action);
            return false;
        }
        case Kind::Capture:
            if (action.on) {
                static_cast<void>(::SetCapture(as_window(action.window)));
            } else if (::ReleaseCapture() == FALSE) {
                failure = refused("ReleaseCapture", action);
                return false;
            }
            return true;
        case Kind::RawInput:
            return register_raw_input(action, failure);
        case Kind::Recentre:
            return recentre(action, failure);
        case Kind::GuestMove:
            move_pointer(action);
            return true;
        case Kind::Click:
            click(action);
            return true;
        case Kind::PointerPrecision:
            return set_precision(action.on, failure);
        case Kind::ActivateShell:
            RWIN_INFO("action", "SetForegroundWindow(taskbar): {}",
                      ::SetForegroundWindow(::FindWindowW(L"Shell_TrayWnd", nullptr)) != FALSE);
            return true;
        default:
            failure = "not a pointer action";
            return false;
    }
}

bool PointerActions::set_class_cursor(const ProbeAction& action, std::string& failure) {
    const std::optional<HCURSOR> cursor = cursors_.cursor_for(action.cursor, failure);
    if (!cursor.has_value()) return false;
    ::SetLastError(0);
    if (::SetClassLongPtrW(as_window(action.window), GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(*cursor)) == 0
        && ::GetLastError() != 0) {
        failure = refused("SetClassLongPtr(GCLP_HCURSOR)", action);
        return false;
    }
    classes_changed_.push_back(action.window);
    return true;
}

bool PointerActions::answer_cursor(const ProbeAction& action, std::string& failure) {
    const std::optional<HCURSOR> cursor = cursors_.cursor_for(action.cursor, failure);
    if (!cursor.has_value()) return false;
    ProbeWindow* window = windows_.made(action.window);
    if (window == nullptr) {
        failure = std::format("{:#x} is not a window the probe made", action.window);
        return false;
    }
    window->answer_cursor_with(*cursor);
    POINT at{};
    ::GetCursorPos(&at);
    if (::GetAncestor(::WindowFromPoint(at), GA_ROOT) == as_window(action.window)) ::SetCursor(*cursor);
    return true;
}

void PointerActions::show_cursor(bool show) {
    display_count_ = ::ShowCursor(show ? TRUE : FALSE);
    RWIN_INFO("action", "ShowCursor({}): the count is {}", show, display_count_);
}

bool PointerActions::set_precision(bool on, std::string& failure) {
    std::array<int, 3> now{};
    if (::SystemParametersInfoW(SPI_GETMOUSE, 0, now.data(), 0) == FALSE) {
        failure = std::format("SPI_GETMOUSE failed: error {}", ::GetLastError());
        return false;
    }
    if (!ballistics_.has_value()) ballistics_ = now;
    std::array<int, 3> wanted{on ? 6 : 0, on ? 10 : 0, on ? 1 : 0};
    if (::SystemParametersInfoW(SPI_SETMOUSE, 0, wanted.data(), SPIF_SENDCHANGE) != FALSE) return true;
    failure = std::format("SPI_SETMOUSE failed: error {}", ::GetLastError());
    return false;
}

bool PointerActions::clip(const ProbeAction& action, std::string& failure) const {
    if (action.width == 0) return ::ClipCursor(nullptr) != FALSE;
    const POINT corner = on_screen(action);
    const RECT area{corner.x, corner.y, corner.x + action.width, corner.y + action.height};
    if (::ClipCursor(&area) != FALSE) return true;
    failure = refused("ClipCursor", action);
    return false;
}

bool PointerActions::register_raw_input(const ProbeAction& action, std::string& failure) {
    RAWINPUTDEVICE mouse{kGenericDesktop, kMouseUsage, action.on ? action.flags : RIDEV_REMOVE,
                         action.on ? as_window(action.window) : nullptr};
    if (::RegisterRawInputDevices(&mouse, 1, sizeof(mouse)) == FALSE) {
        failure = refused("RegisterRawInputDevices", action);
        return false;
    }
    raw_input_ = action.on;
    return true;
}

bool PointerActions::recentre(const ProbeAction& action, std::string& failure) {
    ProbeWindow* window = windows_.made(action.window);
    if (window == nullptr) {
        failure = std::format("{:#x} is not a window the probe made", action.window);
        return false;
    }
    window->recentre(action.on);
    recentring_ = action.on ? action.window : 0;
    if (action.on) put_back_by_ = action.window;
    return true;
}

std::uint32_t PointerActions::put_back() const {
    const ProbeWindow* window = windows_.made(put_back_by_);
    return window == nullptr ? 0 : window->put_back();
}

void PointerActions::move_pointer(const ProbeAction& action) {
    INPUT move = absolute_move(on_screen(action));
    const UINT sent = ::SendInput(1, &move, sizeof(INPUT));
    RWIN_INFO("action", "moved the pointer to client {},{} of {:#x}: {} input taken", action.x, action.y,
              action.window, sent);
}

void PointerActions::click(const ProbeAction& action) {
    const POINT at = on_screen(action);
    std::array<INPUT, 3> inputs{};
    inputs[0] = absolute_move(at);
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[2].type = INPUT_MOUSE;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    const UINT sent = ::SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    RWIN_INFO("action", "clicked {},{} on the screen: {} of {} inputs taken", at.x, at.y, sent, inputs.size());
}

void PointerActions::reset() {
    while (display_count_ < resting_count_) display_count_ = ::ShowCursor(TRUE);
    while (display_count_ > resting_count_) display_count_ = ::ShowCursor(FALSE);
    ::ClipCursor(nullptr);
    ::ReleaseCapture();
    if (raw_input_) {
        RAWINPUTDEVICE mouse{kGenericDesktop, kMouseUsage, RIDEV_REMOVE, nullptr};
        ::RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
        raw_input_ = false;
    }
    HCURSOR arrow = ::LoadCursorA(nullptr, IDC_ARROW);
    for (const std::uint64_t window : classes_changed_) {
        if (::IsWindow(as_window(window)) != FALSE) {
            ::SetClassLongPtrW(as_window(window), GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(arrow));
        }
    }
    classes_changed_.clear();
    recentring_ = 0;
    put_back_by_ = 0;
    if (ballistics_.has_value()) {
        ::SystemParametersInfoW(SPI_SETMOUSE, 0, ballistics_->data(), SPIF_SENDCHANGE);
        ballistics_.reset();
    }
}

}
