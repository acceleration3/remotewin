#pragma once

#include "host/export/pci_address.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rwin::host {
struct ExportGpu {
    std::array<std::uint8_t, 16> uuid{};
    std::optional<PciAddress> pci;

    [[nodiscard]] bool operator==(const ExportGpu&) const noexcept = default;
};

}
