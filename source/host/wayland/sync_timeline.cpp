#include "host/wayland/sync_timeline.hpp"

#include "host/wayland/drm_sync_device.hpp"
#include "logging/logger.hpp"

#include <linux-drm-syncobj-v1-client-protocol.h>
#include <xf86drm.h>

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace rwin::host {

SyncTimeline::SyncTimeline(const DrmSyncDevice& device) : device_(device.fd()) {
    if (drmSyncobjCreate(device_, 0, &handle_) != 0) {
        RWIN_WARN("sync", "no syncobj: {}", std::strerror(errno));
        handle_ = 0;
        return;
    }
    int shared = -1;
    if (drmSyncobjHandleToFD(device_, handle_, &shared) != 0) {
        RWIN_WARN("sync", "a syncobj could not be shared: {}", std::strerror(errno));
        return;
    }
    const UniqueFd handed{shared};
    timeline_ = wp_linux_drm_syncobj_manager_v1_import_timeline(device.manager(), handed.get());
}

SyncTimeline::~SyncTimeline() {
    if (timeline_ != nullptr) wp_linux_drm_syncobj_timeline_v1_destroy(timeline_);
    if (handle_ != 0) drmSyncobjDestroy(device_, handle_);
}

// Through a binary syncobj: a sync_file is imported only into one, whose
// fence is then put at the point.
bool SyncTimeline::signal_with(int sync_file, std::uint64_t point) const {
    std::uint32_t binary = 0;
    if (drmSyncobjCreate(device_, 0, &binary) != 0) return false;
    const bool done = drmSyncobjImportSyncFile(device_, binary, sync_file) == 0
                      && drmSyncobjTransfer(device_, handle_, point, binary, 0, 0) == 0;
    drmSyncobjDestroy(device_, binary);
    if (!done) RWIN_WARN("sync", "a frame's fence could not be put on the timeline: {}", std::strerror(errno));
    return done;
}

void SyncTimeline::signal_now(std::uint64_t point) const {
    std::uint32_t handle = handle_;
    if (drmSyncobjTimelineSignal(device_, &handle, &point, 1) != 0) {
        RWIN_WARN("sync", "a timeline point could not be signalled: {}", std::strerror(errno));
    }
}

UniqueFd SyncTimeline::when_signalled(std::uint64_t point) const {
    UniqueFd signal{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    if (!signal.valid()) return signal;
    if (drmSyncobjEventfd(device_, handle_, point, signal.get(), 0) != 0) {
        RWIN_WARN("sync", "the compositor's release cannot be waited for: {}", std::strerror(errno));
        return UniqueFd{};
    }
    return signal;
}

}  // namespace rwin::host
