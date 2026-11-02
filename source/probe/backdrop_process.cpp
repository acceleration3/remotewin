#include "probe/backdrop_process.hpp"

#include "probe/backdrop_window.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cwchar>
#include <string>

namespace rwin::probe {

namespace {

constexpr DWORD kShowTimeoutMs = 5000;
constexpr DWORD kPollMs = 20;

struct Search {
    DWORD process_id;
    bool found;
};

BOOL CALLBACK find_backdrop(HWND window, LPARAM param) {
    auto* search = reinterpret_cast<Search*>(param);
    DWORD owner = 0;
    ::GetWindowThreadProcessId(window, &owner);
    if (owner != search->process_id || ::IsWindowVisible(window) == FALSE) return TRUE;
    wchar_t name[64] = {};
    ::GetClassNameW(window, name, 64);
    if (std::wcscmp(name, BackdropWindow::kClassName) != 0) return TRUE;
    search->found = true;
    return FALSE;
}

}

BackdropProcess::BackdropProcess(std::uint32_t rgb) {
    wchar_t executable[MAX_PATH] = {};
    ::GetModuleFileNameW(nullptr, executable, MAX_PATH);
    wchar_t arguments[64] = {};
    ::swprintf(arguments, 64, L" --backdrop %06x --parent %lu", rgb & 0xFFFFFF,
               ::GetCurrentProcessId());
    std::wstring command_line = L"\"" + std::wstring{executable} + L"\"" + arguments;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION started{};
    if (::CreateProcessW(executable, command_line.data(), nullptr, nullptr, FALSE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &started)
        == FALSE) {
        RWIN_ERROR("backdrop", "cannot start the backdrop process: error {}", ::GetLastError());
        return;
    }
    ::CloseHandle(started.hThread);
    process_ = started.hProcess;
    process_id_ = started.dwProcessId;
    if (!wait_until_shown()) {
        RWIN_WARN("backdrop", "process {} has shown no backdrop after {} ms", process_id_,
                  kShowTimeoutMs);
    }
}

BackdropProcess::~BackdropProcess() {
    if (process_ == nullptr) return;
    ::TerminateProcess(process_, 0);
    ::WaitForSingleObject(process_, kShowTimeoutMs);
    ::CloseHandle(process_);
}

bool BackdropProcess::wait_until_shown() const {
    for (DWORD waited = 0; waited < kShowTimeoutMs; waited += kPollMs) {
        Search search{process_id_, false};
        ::EnumWindows(&find_backdrop, reinterpret_cast<LPARAM>(&search));
        if (search.found) return true;
        if (::WaitForSingleObject(process_, kPollMs) == WAIT_OBJECT_0) return false;
    }
    return false;
}

}
