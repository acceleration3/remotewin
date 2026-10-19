#pragma once

#include "host/export/unique_fd.hpp"

#include <cstdint>

struct wp_linux_drm_syncobj_timeline_v1;

namespace rwin::host {

class DrmSyncDevice;

// A DRM timeline syncobj shared with the compositor
// (wp_linux_drm_syncobj_manager_v1.import_timeline): points on it are
// signalled by a fence here, or by the compositor, and waited for either
// way without blocking.
class SyncTimeline {
public:
    // valid() false when the syncobj cannot be made or shared.
    explicit SyncTimeline(const DrmSyncDevice& device);
    ~SyncTimeline();

    SyncTimeline(const SyncTimeline&) = delete;
    SyncTimeline& operator=(const SyncTimeline&) = delete;
    SyncTimeline(SyncTimeline&&) = delete;
    SyncTimeline& operator=(SyncTimeline&&) = delete;

    [[nodiscard]] bool valid() const noexcept { return timeline_ != nullptr; }
    [[nodiscard]] wp_linux_drm_syncobj_timeline_v1* timeline() const noexcept { return timeline_; }

    // ``point`` is signalled when the fence of ``sync_file`` is.
    [[nodiscard]] bool signal_with(int sync_file, std::uint64_t point) const;
    // ``point`` is signalled now.
    void signal_now(std::uint64_t point) const;
    // An eventfd readable once ``point`` is signalled; invalid when one
    // cannot be had.
    [[nodiscard]] UniqueFd when_signalled(std::uint64_t point) const;

private:
    int device_;
    std::uint32_t handle_ = 0;
    wp_linux_drm_syncobj_timeline_v1* timeline_ = nullptr;
};

}  // namespace rwin::host
