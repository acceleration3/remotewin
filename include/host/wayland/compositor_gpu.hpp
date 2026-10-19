#pragma once

#include "host/export/pci_address.hpp"

#include <optional>
#include <string>

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

private:
    std::optional<PciAddress> pci_;
    std::string render_node_;
};

}  // namespace rwin::host
