#include "probe/probe_windows.hpp"

#include "probe/window_reading.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <algorithm>
#include <cwchar>

namespace rwin::probe {

namespace {

// The class atom every dialog box is made of, named as the system classes
// are documented: "#32770".
constexpr const wchar_t* kDialogClass = L"#32770";

HWND as_window(std::uint64_t handle) {
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
}

std::uint64_t as_handle(HWND window) {
    return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window));
}

struct DialogSearch {
    const ProbeWindows* windows;
    std::uint64_t found;
};

BOOL CALLBACK look_for_dialog(HWND window, LPARAM param) {
    auto* search = reinterpret_cast<DialogSearch*>(param);
    if (::IsWindowVisible(window) == FALSE || search->windows->has(as_handle(window))) return TRUE;
    wchar_t name[16] = {};
    ::GetClassNameW(window, name, 16);
    if (std::wcscmp(name, kDialogClass) != 0) return TRUE;
    search->found = as_handle(window);
    return FALSE;
}

}  // namespace

void ProbeWindows::adopt(std::unique_ptr<ProbeWindow> window) {
    handles_.push_back(window->handle());
    made_.push_back(std::move(window));
}

void ProbeWindows::follow(std::uint64_t handle) {
    if (!has(handle)) handles_.push_back(handle);
}

bool ProbeWindows::has(std::uint64_t handle) const {
    return std::find(handles_.begin(), handles_.end(), handle) != handles_.end();
}

ProbeWindow* ProbeWindows::made(std::uint64_t handle) const {
    const auto found = std::find_if(made_.begin(), made_.end(),
                                    [handle](const auto& window) { return window->handle() == handle; });
    return found == made_.end() ? nullptr : found->get();
}

std::optional<WindowTruth> ProbeWindows::truth(std::uint64_t handle) const {
    if (!has(handle)) return std::nullopt;
    WindowTruth truth = read_window(as_window(handle));
    if (truth.exists) truth.z_rank = z_rank(as_window(handle));
    return truth;
}

std::int32_t ProbeWindows::z_rank(HWND window) const {
    std::int32_t rank = 0;
    for (HWND at = ::GetTopWindow(nullptr); at != nullptr; at = ::GetWindow(at, GW_HWNDNEXT)) {
        if (at == window) return rank;
        if (has(as_handle(at))) ++rank;
    }
    return -1;
}

std::uint64_t ProbeWindows::new_dialog() const {
    DialogSearch search{this, 0};
    ::EnumThreadWindows(::GetCurrentThreadId(), &look_for_dialog,
                        reinterpret_cast<LPARAM>(&search));
    return search.found;
}

bool ProbeWindows::destroy(std::uint64_t handle) {
    const auto made = std::find_if(made_.begin(), made_.end(),
                                   [handle](const auto& window) { return window->handle() == handle; });
    if (made != made_.end()) {
        made_.erase(made);
        return true;
    }
    if (!has(handle)) return false;
    return ::DestroyWindow(as_window(handle)) != FALSE;
}

void ProbeWindows::destroy_all() {
    for (auto it = handles_.rbegin(); it != handles_.rend(); ++it) {
        const bool made = std::any_of(made_.begin(), made_.end(),
                                      [&](const auto& window) { return window->handle() == *it; });
        if (!made && ::IsWindow(as_window(*it)) != FALSE) ::DestroyWindow(as_window(*it));
    }
    while (!made_.empty()) made_.pop_back();
    handles_.clear();
}

}  // namespace rwin::probe
