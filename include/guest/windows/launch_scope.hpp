#pragma once

#include <cstdint>
#include <string>

namespace rwin::guest {

// Which processes belong to the application being mirrored.
//
// A process id alone is not the answer. What gets launched is often a
// stub that spawns the real program and exits, so by the time a window
// appears the id that was started is gone and its parentage with it. A
// Job Object survives that: whatever a member starts joins the job, and
// stays in it.
//
// The job is opened by name because it belongs to whoever did the
// launching. This side only asks questions of it.
//
// Constructed with neither a name nor an id, it contains everything —
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
    [[nodiscard]] bool everything() const noexcept {
        return job_ == nullptr && process_id_ == 0;
    }

private:
    void* job_ = nullptr;
    std::uint32_t process_id_ = 0;
};

}  // namespace rwin::guest
