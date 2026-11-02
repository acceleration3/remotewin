#include "guest/windows/process_tree.hpp"

// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on

#include <algorithm>
#include <array>
#include <set>
#include <string_view>

namespace rwin::guest {

namespace {
constexpr std::size_t kMostRecords = 8192;
constexpr std::uint64_t kEverythingEveryMs = 1000;

struct BasicInformation {
    NTSTATUS exit_status;
    void* peb;
    ULONG_PTR affinity;
    LONG priority;
    ULONG_PTR process_id;
    ULONG_PTR parent_process_id;
};
constexpr ULONG kProcessBasicInformation = 0;
using QueryInformationProcess = LONG(NTAPI*)(HANDLE, ULONG, void*, ULONG, ULONG*);

QueryInformationProcess query_information_process() {
    static const auto query = reinterpret_cast<QueryInformationProcess>(reinterpret_cast<void*>(
        ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess")));
    return query;
}

std::uint64_t created_at(HANDLE process) {
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernel{};
    FILETIME user{};
    if (::GetProcessTimes(process, &created, &exited, &kernel, &user) == FALSE) return 0;
    return (static_cast<std::uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

std::uint64_t now_filetime() {
    FILETIME now{};
    ::GetSystemTimeAsFileTime(&now);
    return (static_cast<std::uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
}

std::wstring_view file_name_of(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L'\\');
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

}

std::optional<ProcessTree::Record> ProcessTree::remembered(std::uint32_t pid, std::uint64_t child_created) const {
    const auto known = records_.find(pid);
    if (known == records_.end()) return std::nullopt;
    std::optional<Record> found;
    for (const Record& record : known->second) {
        if (record.created <= child_created && record.last_seen >= child_created) found = record;
    }
    return found;
}

void ProcessTree::prune() {
    std::set<Key> needed;
    for (const auto& [pid, processes] : records_) {
        for (const Record& alive : processes) {
            if (alive.last_seen < last_snapshot_) continue;
            Record at = alive;
            for (std::size_t depth = 0; depth < kMostDepth * 4 && at.parent != 0; ++depth) {
                const std::optional<Record> parent = remembered(at.parent, at.created);
                if (!parent.has_value() || !needed.insert(Key{at.parent, parent->created}).second) break;
                at = *parent;
            }
        }
    }
    struct Dead {
        std::uint64_t last_seen;
        Key key;
    };
    std::vector<Dead> dead;
    for (const auto& [pid, processes] : records_) {
        for (const Record& record : processes) {
            if (record.last_seen < last_snapshot_ && !needed.contains(Key{pid, record.created}))
                dead.push_back(Dead{record.last_seen, Key{pid, record.created}});
        }
    }
    std::sort(dead.begin(), dead.end(), [](const Dead& a, const Dead& b) { return a.last_seen < b.last_seen; });
    for (const Dead& gone : dead) {
        std::vector<Record>& processes = records_[gone.key.first];
        std::erase_if(processes, [&](const Record& record) { return record.created == gone.key.second; });
        if (processes.empty()) records_.erase(gone.key.first);
        --kept_;
    }
}

void ProcessTree::keep(std::uint32_t pid, const Record& record) {
    if (kept_ >= kMostRecords) prune();
    records_[pid].push_back(record);
    ++kept_;
}

std::optional<ProcessTree::Record> ProcessTree::learn(std::uint32_t pid) {
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr) return std::nullopt;
    const std::uint64_t created = created_at(process);
    const std::uint64_t now = now_filetime();
    if (created != 0) {
        const auto known = records_.find(pid);
        if (known != records_.end()) {
            for (Record& record : known->second) {
                if (record.created == created) {
                    ::CloseHandle(process);
                    record.last_seen = now;
                    return record;
                }
            }
        }
    }
    Record record;
    record.created = created;
    record.last_seen = now;
    BasicInformation information{};
    ULONG returned = 0;
    const QueryInformationProcess query = query_information_process();
    if (created != 0 && query != nullptr
        && query(process, kProcessBasicInformation, &information, sizeof(information), &returned) >= 0) {
        record.parent = static_cast<std::uint32_t>(information.parent_process_id);
    }
    std::array<wchar_t, MAX_PATH * 2> path{};
    DWORD length = static_cast<DWORD>(path.size());
    record.boundary = ::QueryFullProcessImageNameW(process, 0, path.data(), &length) == FALSE
                      || boundary_.contains(file_name_of(std::wstring_view(path.data(), length)));
    ::CloseHandle(process);
    if (created == 0) return std::nullopt;
    keep(pid, record);
    return record;
}

void ProcessTree::learn_everything_alive() {
    const std::uint64_t now = ::GetTickCount64();
    if (learned_all_at_ != 0 && now - learned_all_at_ < kEverythingEveryMs) return;
    learned_all_at_ = now;
    last_snapshot_ = now_filetime();
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = ::Process32FirstW(snapshot, &entry); more != FALSE; more = ::Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID != 0) learn(entry.th32ProcessID);
    }
    ::CloseHandle(snapshot);
}

std::vector<WindowFields::ProcessRef> ProcessTree::tree_of(std::uint32_t pid) {
    learn_everything_alive();
    std::optional<Record> at = learn(pid);
    if (!at.has_value()) return {};
    std::vector<WindowFields::ProcessRef> tree{{pid, at->created}};
    if (at->boundary) return tree;
    while (tree.size() <= kMostDepth && at->parent != 0) {
        std::optional<Record> parent = remembered(at->parent, at->created);
        if (!parent.has_value() && learn(at->parent).has_value()) parent = remembered(at->parent, at->created);
        if (!parent.has_value() || parent->boundary) break;
        tree.push_back({at->parent, parent->created});
        at = parent;
    }
    return tree;
}

}
