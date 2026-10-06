#include "guest/windows/launch_scope.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <vector>

namespace rwin::guest {

namespace {

// Parent links are walked until nothing new is counted; in practice one
// or two passes.
constexpr int kMostParentPasses = 8;

struct Process {
    std::uint32_t id = 0;
    std::uint32_t parent = 0;
};

std::vector<Process> running_processes() {
    std::vector<Process> found;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return found;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = ::Process32FirstW(snapshot, &entry); more != FALSE;
         more = ::Process32NextW(snapshot, &entry)) {
        found.push_back(Process{entry.th32ProcessID, entry.th32ParentProcessID});
    }
    ::CloseHandle(snapshot);
    return found;
}

std::wstring lowercase(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

// A process's image path, lowercase, and when it started; false for one
// that cannot be opened.
bool describe(std::uint32_t process_id, std::wstring& image, std::uint64_t& started) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) return false;
    wchar_t path[MAX_PATH * 2] = {};
    DWORD length = static_cast<DWORD>(std::size(path));
    const bool named = ::QueryFullProcessImageNameW(process, 0, path, &length) != FALSE;
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    const bool timed = ::GetProcessTimes(process, &created, &exited, &kernel, &user) != FALSE;
    ::CloseHandle(process);
    if (!named || !timed) return false;
    image = lowercase(std::wstring(path, length));
    started = (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return true;
}

std::wstring windows_directory() {
    wchar_t path[MAX_PATH] = {};
    const UINT length = ::GetWindowsDirectoryW(path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return lowercase(std::wstring(path, length)) + L'\\';
}

bool in_job(void* job, std::uint32_t process_id) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) return false;
    BOOL member = FALSE;
    const BOOL asked = ::IsProcessInJob(process, static_cast<HANDLE>(job), &member);
    ::CloseHandle(process);
    return asked != FALSE && member != FALSE;
}

}  // namespace

LaunchScope::LaunchScope(const std::string& job_name, std::uint32_t process_id)
    : process_id_(process_id) {
    std::wstring image;
    if (process_id != 0 && describe(process_id, image, started_)) {
        const std::wstring system = windows_directory();
        const std::size_t separator = image.find_last_of(L'\\');
        if (separator != std::wstring::npos && (system.empty() || image.rfind(system, 0) != 0)) {
            directory_ = image.substr(0, separator + 1);
        }
    }
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

void LaunchScope::refresh() const {
    const auto now = std::chrono::steady_clock::now();
    if (now - refreshed_ < kRefreshAfter) return;
    refreshed_ = now;

    const std::vector<Process> processes = running_processes();
    std::unordered_map<std::uint32_t, std::uint32_t> parent_of;
    parent_of.reserve(processes.size());
    for (const Process& process : processes) parent_of.emplace(process.id, process.parent);

    std::unordered_set<std::uint32_t> counted;
    for (const std::uint32_t member : members_) {
        if (parent_of.contains(member)) counted.insert(member);
    }
    if (parent_of.contains(process_id_)) counted.insert(process_id_);
    for (const std::uint32_t adopted : adopted_) {
        if (parent_of.contains(adopted)) counted.insert(adopted);
    }

    std::unordered_map<std::uint32_t, std::uint64_t> started;
    const auto start_of = [&started](std::uint32_t id) -> std::uint64_t {
        if (const auto known = started.find(id); known != started.end()) return known->second;
        std::wstring image;
        std::uint64_t when = 0;
        if (!describe(id, image, when)) when = 0;
        started.emplace(id, when);
        return when;
    };

    for (const Process& process : processes) {
        if (counted.contains(process.id)) continue;
        if (job_ != nullptr && in_job(job_, process.id)) {
            counted.insert(process.id);
            continue;
        }
        if (directory_.empty()) continue;
        std::wstring image;
        std::uint64_t when = 0;
        if (describe(process.id, image, when) && when >= started_ && image.rfind(directory_, 0) == 0) {
            counted.insert(process.id);
        }
    }

    for (int pass = 0; pass < kMostParentPasses; ++pass) {
        bool grew = false;
        for (const Process& process : processes) {
            if (counted.contains(process.id) || !counted.contains(process.parent)) continue;
            // A recycled parent id would otherwise adopt a process older
            // than the parent it names.
            const std::uint64_t child = start_of(process.id);
            const std::uint64_t parent = start_of(process.parent);
            if (child != 0 && parent != 0 && child < parent) continue;
            counted.insert(process.id);
            grew = true;
        }
        if (!grew) break;
    }

    for (const std::uint32_t id : counted) {
        if (!members_.contains(id) && id != process_id_) {
            RWIN_INFO("scope", "pid {} belongs to the application too", id);
        }
    }
    members_ = std::move(counted);
}

bool LaunchScope::adopt(std::uint32_t process_id) {
    if (everything() || process_id == 0 || contains(process_id)) return false;
    std::wstring image;
    std::uint64_t started = 0;
    if (!describe(process_id, image, started)) return false;
    const std::wstring system = windows_directory();
    if (!system.empty() && image.rfind(system, 0) == 0) return false;
    adopted_.insert(process_id);
    members_.insert(process_id);
    RWIN_INFO("scope", "pid {} ({}) took the foreground from the application; it belongs to it "
              "too", process_id, std::string(image.begin(), image.end()));
    return true;
}

bool LaunchScope::alive() const {
    if (everything()) return true;
    refresh();
    return !members_.empty();
}

bool LaunchScope::contains(std::uint32_t process_id) const {
    if (everything()) return true;
    refresh();
    if (members_.contains(process_id)) return true;
    // A process started since the last refresh, before its window was
    // listed: the job answers for its members at once.
    return (job_ != nullptr && in_job(job_, process_id)) ||
           (process_id_ != 0 && process_id == process_id_) || adopted_.contains(process_id);
}

}  // namespace rwin::guest
