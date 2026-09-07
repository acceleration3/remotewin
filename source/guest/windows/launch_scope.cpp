#include "guest/windows/launch_scope.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

LaunchScope::LaunchScope(const std::string& job_name, std::uint32_t process_id)
    : process_id_(process_id) {
    if (job_name.empty()) return;

    const int wide_length =
        ::MultiByteToWideChar(CP_UTF8, 0, job_name.c_str(),
                              static_cast<int>(job_name.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(wide_length), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, job_name.c_str(),
                          static_cast<int>(job_name.size()), wide.data(), wide_length);

    job_ = ::OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, wide.c_str());
    if (job_ == nullptr) {
        RWIN_WARN("scope", "no job named '{}' (error {}); falling back to pid {}",
                  job_name, ::GetLastError(), process_id);
        return;
    }
    RWIN_INFO("scope", "watching the windows of job '{}'", job_name);
}

LaunchScope::~LaunchScope() {
    if (job_ != nullptr) ::CloseHandle(job_);
}

bool LaunchScope::alive() const {
    if (everything()) return true;

    if (job_ != nullptr) {
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        if (::QueryInformationJobObject(job_, JobObjectBasicAccountingInformation,
                                        &accounting, sizeof(accounting), nullptr)
            != FALSE) {
            return accounting.ActiveProcesses > 0;
        }
    }
    HANDLE process =
        ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id_);
    if (process == nullptr) return false;
    DWORD exit_code = 0;
    const bool running = ::GetExitCodeProcess(process, &exit_code) != FALSE
                         && exit_code == STILL_ACTIVE;
    ::CloseHandle(process);
    return running;
}

bool LaunchScope::contains(std::uint32_t process_id) const {
    if (everything()) return true;

    if (job_ != nullptr) {
        HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                       process_id);
        if (process != nullptr) {
            BOOL member = FALSE;
            const BOOL asked = ::IsProcessInJob(process, job_, &member);
            ::CloseHandle(process);
            if (asked != FALSE && member != FALSE) return true;
        }
    }
    // The launched id itself, for the case where the job could not be
    // opened at all and for the moment before a stub's payload exists.
    return process_id_ != 0 && process_id == process_id_;
}

}  // namespace rwin::guest
