#pragma once

#include "host/export/pci_address.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace rwin::host {

class Globals;

// The GPU the compositor composes on, as it says itself
// (zwp_linux_dmabuf_v1 version 4, its default feedback's main device): what
// the guest's surfaces have to be on for their dma-bufs to be shown as they
// are, and the device whose DRM syncobjs explicit sync is done with.
// Nothing is known where the compositor does not say.
class CompositorGpu {
public:
    // A round trip on the connection.
    explicit CompositorGpu(const Globals& globals);

    [[nodiscard]] const std::optional<PciAddress>& pci() const noexcept { return pci_; }
    // Its render node (/dev/dri/renderD128); empty when not known.
    [[nodiscard]] const std::string& render_node() const noexcept { return render_node_; }
    // Whether the compositor takes a buffer of DRM format ``fourcc`` laid
    // out by ``modifier`` from its main device's tranches.
    [[nodiscard]] bool offers(std::uint32_t fourcc, std::uint64_t modifier) const {
        return formats_.contains({fourcc, modifier});
    }

private:
    std::optional<PciAddress> pci_;
    std::string render_node_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> formats_;
};

}  // namespace rwin::host
