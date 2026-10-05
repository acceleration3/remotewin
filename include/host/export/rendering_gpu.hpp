#pragma once

#include "host/export/pci_address.hpp"

#include <optional>
#include <string>

namespace rwin::host {

struct ExportGpu;

// The GPU this process draws its windows on, as Qt's EGL display names it,
// and whether that display can take what the GPU device exports.
//
// The device's surfaces come with the layout their driver kept with the
// memory (an implicit modifier), which only the same GPU can read. So
// frames stay on the GPU only when the two are one: the PCI address of the
// display's DRM device is the one the device's hello says.
class RenderingGpu {
public:
    // Asks through a context of Qt's own, on the display every window here
    // is drawn on. Needs the QGuiApplication.
    RenderingGpu();

    // Why frames from ``exported`` cannot be drawn here, or nothing when
    // they can.
    [[nodiscard]] std::optional<std::string> refusal_for(const ExportGpu& exported) const;

private:
    std::optional<PciAddress> pci_;
    // What this side lacks to import at all; empty when nothing.
    std::string lacking_;
};

}  // namespace rwin::host
