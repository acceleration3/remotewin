#include "guest/window_enumerator.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

std::string text_of(HWND window, int (*get)(HWND, wchar_t*, int)) {
    wchar_t wide[512] = {};
    const int length = get(window, wide, 512);
    if (length <= 0) return {};
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0,
                                            nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide, length, out.data(), bytes, nullptr, nullptr);
    return out;
}

int window_title(HWND window, wchar_t* into, int size) {
    return ::GetWindowTextW(window, into, size);
}

int window_class(HWND window, wchar_t* into, int size) {
    return ::GetClassNameW(window, into, size);
}

bool worth_showing(HWND window) {
    if (::IsWindowVisible(window) == FALSE) return false;
    // An owned tool window is chrome for another window, not a window of
    // its own; the host draws those as part of their owner.
    const LONG_PTR extended = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((extended & WS_EX_TOOLWINDOW) != 0) return false;
    RECT rect{};
    if (::GetWindowRect(window, &rect) == FALSE) return false;
    return rect.right > rect.left && rect.bottom > rect.top;
}

struct Collector {
    std::vector<GuestWindow>* into;
    std::uint32_t only_process;
};

BOOL CALLBACK collect(HWND window, LPARAM param) {
    auto* collector = reinterpret_cast<Collector*>(param);
    if (!worth_showing(window)) return TRUE;

    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);
    if (collector->only_process != 0 && process_id != collector->only_process) {
        return TRUE;
    }

    RECT rect{};
    ::GetWindowRect(window, &rect);

    GuestWindow found;
    found.handle = reinterpret_cast<std::uintptr_t>(window);
    found.process_id = process_id;
    found.x = rect.left;
    found.y = rect.top;
    found.width = static_cast<std::uint32_t>(rect.right - rect.left);
    found.height = static_cast<std::uint32_t>(rect.bottom - rect.top);
    found.style = static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_STYLE));
    found.extended_style =
        static_cast<std::uint32_t>(::GetWindowLongPtrW(window, GWL_EXSTYLE));
    found.title = text_of(window, window_title);
    found.class_name = text_of(window, window_class);
    collector->into->push_back(std::move(found));
    return TRUE;
}

}  // namespace

std::vector<GuestWindow> WindowEnumerator::visible_windows() const {
    return windows_of(0);
}

std::vector<GuestWindow> WindowEnumerator::windows_of(std::uint32_t process_id) const {
    std::vector<GuestWindow> found;
    Collector collector{&found, process_id};
    ::EnumWindows(&collect, reinterpret_cast<LPARAM>(&collector));
    return found;
}

}  // namespace rwin::guest
