#pragma once

#include "host/export/pci_address.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rwin::host {

// The GPU the device's surfaces are on, as its hello names it.
struct ExportGpu {
    // VkPhysicalDeviceIDProperties::deviceUUID.
    std::array<std::uint8_t, 16> uuid{};
    // Nothing for a GPU on no PCI bus.
    std::optional<PciAddress> pci;

    [[nodiscard]] bool operator==(const ExportGpu&) const noexcept = default;
};

}  // namespace rwin::host
