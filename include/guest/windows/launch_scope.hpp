#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace rwin::guest {

// Which processes belong to the application being mirrored.
//
// A process id alone is not the answer. What gets launched is often a
// stub that spawns the real program and exits, so by the time a window
// appears the id that was started is gone and its parentage with it. No
// one rule covers what real applications do, so three are kept, as the
// agent that launched it keeps them:
//
//   - Job membership: whatever a member starts joins the job. The job is
//     opened by name because it belongs to whoever did the launching;
//     this side only asks questions of it.
//   - Descent from a process already counted: a stub that relaunches
//     itself through a broker - Steam's updater restarting Steam - leaves
//     the new process outside the job with its parent link intact.
//   - Running out of the launched program's own directory, started after
//     it: the backstop once even the parent link is gone. Not for a
//     program in the Windows directory, where it would take in every
//     system process started since.
//
// A process counted stays counted while it runs, so an intermediate
// exiting cannot orphan what it started.
//
// Constructed with neither a name nor an id, it contains everything -
// which is what mirroring a whole desktop means.
class LaunchScope {
public:
    LaunchScope() = default;
    LaunchScope(const std::string& job_name, std::uint32_t process_id);
    ~LaunchScope();

    LaunchScope(const LaunchScope&) = delete;
    LaunchScope& operator=(const LaunchScope&) = delete;
    LaunchScope(LaunchScope&&) = delete;
    LaunchScope& operator=(LaunchScope&&) = delete;

    [[nodiscard]] bool contains(std::uint32_t process_id) const;

    // Whether anything in the scope is still running. A stub's own window
    // can come and go before the program it started has drawn a thing,
    // so the windows say nothing about whether the application is done.
    // Always true for everything.
    [[nodiscard]] bool alive() const;
    [[nodiscard]] bool everything() const noexcept {
        return job_ == nullptr && process_id_ == 0;
    }

private:
    // The processes counted, worked out again when the last answer is
    // older than kRefreshAfter: the window list asks about every window
    // it finds, many times a second.
    void refresh() const;

    static constexpr std::chrono::milliseconds kRefreshAfter{200};

    void* job_ = nullptr;
    std::uint32_t process_id_ = 0;
    // The launched program's directory, lowercase and ending in a
    // separator; empty when the directory rule does not apply.
    std::wstring directory_;
    // When the launched program started (FILETIME ticks).
    std::uint64_t started_ = 0;
    mutable std::unordered_set<std::uint32_t> members_;
    mutable std::chrono::steady_clock::time_point refreshed_{};
};

}  // namespace rwin::guest
