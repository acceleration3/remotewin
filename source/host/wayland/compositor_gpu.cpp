#include "host/wayland/compositor_gpu.hpp"

#include "host/wayland/globals.hpp"
#include "logging/logger.hpp"

#include <linux-dmabuf-v1-client-protocol.h>
#include <wayland-client.h>
#include <xf86drm.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace rwin::host {

namespace {
struct TableEntry {
    std::uint32_t format;
    std::uint32_t padding;
    std::uint64_t modifier;
};
static_assert(sizeof(TableEntry) == 16);

struct Feedback {
    std::optional<dev_t> device;
    std::vector<TableEntry> table;
    bool on_main = false;
    std::set<std::pair<std::uint32_t, std::uint64_t>> formats;
    bool done = false;
};

void done(void* data, zwp_linux_dmabuf_feedback_v1*) { static_cast<Feedback*>(data)->done = true; }
void table(void* data, zwp_linux_dmabuf_feedback_v1*, std::int32_t fd, std::uint32_t size) {
    Feedback& feedback = *static_cast<Feedback*>(data);
    void* mapped = size >= sizeof(TableEntry) ? ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    ::close(fd);
    if (mapped == MAP_FAILED) return;
    feedback.table.resize(size / sizeof(TableEntry));
    std::memcpy(feedback.table.data(), mapped, feedback.table.size() * sizeof(TableEntry));
    ::munmap(mapped, size);
}
void main_device(void* data, zwp_linux_dmabuf_feedback_v1*, wl_array* device) {
    if (device->size != sizeof(dev_t)) return;
    dev_t number{};
    std::memcpy(&number, device->data, sizeof(number));
    static_cast<Feedback*>(data)->device = number;
}
void tranche_done(void* data, zwp_linux_dmabuf_feedback_v1*) { static_cast<Feedback*>(data)->on_main = false; }
void tranche_device(void* data, zwp_linux_dmabuf_feedback_v1*, wl_array* device) {
    Feedback& feedback = *static_cast<Feedback*>(data);
    feedback.on_main = false;
    if (device->size != sizeof(dev_t) || !feedback.device.has_value()) return;
    dev_t number{};
    std::memcpy(&number, device->data, sizeof(number));
    feedback.on_main = number == *feedback.device;
}
void tranche_formats(void* data, zwp_linux_dmabuf_feedback_v1*, wl_array* indices) {
    Feedback& feedback = *static_cast<Feedback*>(data);
    if (!feedback.on_main) return;
    const auto* first = static_cast<const std::uint16_t*>(indices->data);
    for (std::size_t at = 0; at < indices->size / sizeof(std::uint16_t); ++at) {
        if (first[at] < feedback.table.size()) {
            feedback.formats.emplace(feedback.table[first[at]].format, feedback.table[first[at]].modifier);
        }
    }
}
void tranche_flags(void*, zwp_linux_dmabuf_feedback_v1*, std::uint32_t) {}

constexpr std::uint32_t kFeedbackVersion = 4;

}

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
    formats_ = std::move(feedback.formats);
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

}
