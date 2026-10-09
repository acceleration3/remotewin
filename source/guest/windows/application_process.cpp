#include "guest/windows/application_process.hpp"

#include <windows.h>

#include <cwchar>

namespace rwin::guest {

std::uint32_t application_process_of(std::uint64_t window_handle) {
    HWND window = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(window_handle));
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(window, &process_id);

    wchar_t class_name[32] = {};
    if (::GetClassNameW(window, class_name, 32) <= 0 ||
        std::wcscmp(class_name, L"ApplicationFrameWindow") != 0) {
        return process_id;
    }
    // Absent while the application is suspended or still starting: the
    // frame is then nobody's but its host's.
    HWND content = ::FindWindowExW(window, nullptr, L"Windows.UI.Core.CoreWindow", nullptr);
    if (content == nullptr) return process_id;
    DWORD content_process = 0;
    ::GetWindowThreadProcessId(content, &content_process);
    return content_process;
}

}  // namespace rwin::guest
