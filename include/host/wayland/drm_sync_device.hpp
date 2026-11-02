#pragma once

#include "host/export/unique_fd.hpp"

#include <string>

struct wp_linux_drm_syncobj_manager_v1;

namespace rwin::host {
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

}
