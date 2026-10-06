#include "guest/windows/foreground_hand_off.hpp"

#include "guest/windows/launch_scope.hpp"

#include <windows.h>

namespace rwin::guest {

void ForegroundHandOff::note_input() noexcept {
    last_input_ = ::GetTickCount();
}

std::optional<std::uint32_t> ForegroundHandOff::handed_to(const LaunchScope& scope) {
    HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr || foreground == foreground_) return std::nullopt;
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(foreground, &process_id);
    const bool was_in_scope = foreground_in_scope_;
    HWND previous = static_cast<HWND>(foreground_);
    const bool in_scope = scope.contains(process_id);
    foreground_ = foreground;
    foreground_in_scope_ = in_scope;
    if (!was_in_scope || in_scope || last_input_ == 0) return std::nullopt;
    // A window closed, hidden or minimised passes the foreground to
    // whatever window is next, which the application did not choose.
    if (::IsWindow(previous) == FALSE || ::IsWindowVisible(previous) == FALSE ||
        ::IsIconic(previous) != FALSE) {
        return std::nullopt;
    }

    const DWORD now = ::GetTickCount();
    if (now - last_input_ > kWithinMs) return std::nullopt;
    LASTINPUTINFO input{};
    input.cbSize = sizeof(input);
    if (::GetLastInputInfo(&input) == FALSE) return std::nullopt;
    // Unsigned: input older than this mirror's wraps to a large number and
    // is as good as the same input.
    const DWORD since_ours = input.dwTime - last_input_;
    if (since_ours > kSameInputMs && since_ours < kWithinMs) return std::nullopt;
    return static_cast<std::uint32_t>(process_id);
}

}  // namespace rwin::guest
