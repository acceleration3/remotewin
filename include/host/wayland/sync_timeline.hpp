#pragma once

#include "host/export/unique_fd.hpp"

#include <cstdint>

struct wp_linux_drm_syncobj_timeline_v1;

namespace rwin::host {

class DrmSyncDevice;

class SyncTimeline {
public:
    explicit SyncTimeline(const DrmSyncDevice& device);
    ~SyncTimeline();

    SyncTimeline(const SyncTimeline&) = delete;
    SyncTimeline& operator=(const SyncTimeline&) = delete;
    SyncTimeline(SyncTimeline&&) = delete;
    SyncTimeline& operator=(SyncTimeline&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return timeline_ != nullptr; }
    [[nodiscard]] wp_linux_drm_syncobj_timeline_v1* timeline() const noexcept { return timeline_; }

    [[nodiscard]] bool signal_with(int sync_file, std::uint64_t point) const;
    void signal_now(std::uint64_t point) const;
    [[nodiscard]] UniqueFd when_signalled(std::uint64_t point) const;

private:
    int device_;
    std::uint32_t handle_ = 0;
    wp_linux_drm_syncobj_timeline_v1* timeline_ = nullptr;
};

}
