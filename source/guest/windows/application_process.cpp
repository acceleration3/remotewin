#include "guest/windows/application_process.hpp"

#include <windows.h>

#include <cwchar>
#include <iterator>

namespace rwin::guest {

std::uint32_t ApplicationProcess::of(std::uint64_t window_handle) {
    HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);

    wchar_t class_name[32] = {};
    if (::GetClassNameW(window, class_name, 32) <= 0 ||
        std::wcscmp(class_name, L"ApplicationFrameWindow") != 0) {
        return process_id;
    }
    HWND content = ::FindWindowExW(window, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
    if (content == nullptr) {
        // Suspended, or still starting: the frame is its host's until an
        // application has been seen in it.
        const auto known = frames_.find(window_handle);
        return known == frames_.end() ? process_id : known->second;
    }
    DWORD content_process = 0;
    ::GetWindowThreadProcessId(content, &content_process);
    frames_[window_handle] = content_process;
    return content_process;
}

void ApplicationProcess::forget_closed() {
    for (auto it = frames_.begin(); it != frames_.end();) {
        HWND frame = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(it->first));
        it = ::IsWindow(frame) == FALSE ? frames_.erase(it) : std::next(it);
    }
}

}  // namespace rwin::guest
