#pragma once

#include "host/export/unique_fd.hpp"

#include <string>

struct wp_linux_drm_syncobj_manager_v1;

namespace rwin::host {

// The DRM device explicit sync is done on (wp_linux_drm_syncobj_v1): the
// compositor's render node, opened here for its timeline syncobjs. Not
// usable where the compositor lacks the protocol, the node cannot be
// opened, or its driver has no timeline syncobjs; frames are then waited
// for here before they are shown.
class DrmSyncDevice {
public:
    DrmSyncDevice(const std::string& render_node, wp_linux_drm_syncobj_manager_v1* manager);

    [[nodiscard]] bool usable() const noexcept { return manager_ != nullptr && device_.valid(); }
    [[nodiscard]] int fd() const noexcept { return device_.get(); }
    [[nodiscard]] wp_linux_drm_syncobj_manager_v1* manager() const noexcept { return manager_; }

private:
    UniqueFd device_;
    wp_linux_drm_syncobj_manager_v1* manager_ = nullptr;
};

}  // namespace rwin::host
