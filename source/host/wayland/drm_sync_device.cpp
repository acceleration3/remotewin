#include "host/wayland/drm_sync_device.hpp"

#include "logging/logger.hpp"

#include <xf86drm.h>

#include <fcntl.h>

#include <cerrno>
#include <cstring>

namespace rwin::host {

DrmSyncDevice::DrmSyncDevice(const std::string& render_node, wp_linux_drm_syncobj_manager_v1* manager) {
    if (manager == nullptr) {
        RWIN_INFO("sync", "the compositor has no explicit sync: frames are waited for here");
        return;
    }
    if (render_node.empty()) {
        RWIN_INFO("sync", "the compositor's GPU is not known: frames are waited for here");
        return;
    }
    UniqueFd device{::open(render_node.c_str(), O_RDWR | O_CLOEXEC)};
    if (!device.valid()) {
        RWIN_WARN("sync", "cannot open {}: {}; frames are waited for here", render_node, std::strerror(errno));
        return;
    }
    std::uint64_t timelines = 0;
    if (drmGetCap(device.get(), DRM_CAP_SYNCOBJ_TIMELINE, &timelines) != 0 || timelines == 0) {
        RWIN_INFO("sync", "{} has no timeline syncobjs: frames are waited for here", render_node);
        return;
    }
    device_ = std::move(device);
    manager_ = manager;
    RWIN_INFO("sync", "explicit sync on {}", render_node);
}

}
