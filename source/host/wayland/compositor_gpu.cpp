#include "host/wayland/compositor_gpu.hpp"

#include "host/wayland/globals.hpp"
#include "logging/logger.hpp"

#include <linux-dmabuf-v1-client-protocol.h>
#include <wayland-client.h>
#include <xf86drm.h>

#include <sys/types.h>
#include <unistd.h>

#include <cstring>

namespace rwin::host {

namespace {

struct Feedback {
    std::optional<dev_t> device;
    bool done = false;
};

void done(void* data, zwp_linux_dmabuf_feedback_v1*) { static_cast<Feedback*>(data)->done = true; }
void table(void*, zwp_linux_dmabuf_feedback_v1*, std::int32_t fd, std::uint32_t) { ::close(fd); }
void main_device(void* data, zwp_linux_dmabuf_feedback_v1*, wl_array* device) {
    if (device->size != sizeof(dev_t)) return;
    dev_t number{};
    std::memcpy(&number, device->data, sizeof(number));
    static_cast<Feedback*>(data)->device = number;
}
void tranche_done(void*, zwp_linux_dmabuf_feedback_v1*) {}
void tranche_device(void*, zwp_linux_dmabuf_feedback_v1*, wl_array*) {}
void tranche_formats(void*, zwp_linux_dmabuf_feedback_v1*, wl_array*) {}
void tranche_flags(void*, zwp_linux_dmabuf_feedback_v1*, std::uint32_t) {}

constexpr std::uint32_t kFeedbackVersion = 4;

}  // namespace

CompositorGpu::CompositorGpu(const Globals& globals) {
    if (globals.dmabuf() == nullptr || globals.dmabuf_version() < kFeedbackVersion) {
        RWIN_INFO("export", "the compositor does not say which GPU it composes on");
        return;
    }
    Feedback feedback;
    zwp_linux_dmabuf_feedback_v1* asked = zwp_linux_dmabuf_v1_get_default_feedback(globals.dmabuf());
    static constexpr zwp_linux_dmabuf_feedback_v1_listener kListener{
        &done, &table, &main_device, &tranche_done, &tranche_device, &tranche_formats, &tranche_flags};
    zwp_linux_dmabuf_feedback_v1_add_listener(asked, &kListener, &feedback);
    while (!feedback.done && wl_display_roundtrip(globals.display()) >= 0) {
    }
    zwp_linux_dmabuf_feedback_v1_destroy(asked);
    if (!feedback.device.has_value()) return;
    drmDevicePtr device = nullptr;
    if (drmGetDeviceFromDevId(*feedback.device, 0, &device) != 0 || device == nullptr) return;
    if (device->bustype == DRM_BUS_PCI && device->businfo.pci != nullptr) {
        pci_ = PciAddress{device->businfo.pci->domain, device->businfo.pci->bus, device->businfo.pci->dev,
                          device->businfo.pci->func};
    }
    if ((device->available_nodes & (1 << DRM_NODE_RENDER)) != 0) render_node_ = device->nodes[DRM_NODE_RENDER];
    drmFreeDevice(&device);
    RWIN_INFO("export", "the compositor composes on {} ({})", pci_.has_value() ? pci_->to_string() : "no PCI device",
              render_node_.empty() ? "no render node" : render_node_);
}

}  // namespace rwin::host
