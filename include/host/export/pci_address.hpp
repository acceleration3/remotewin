#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rwin::host {

// Where a GPU is on the PCI bus: what says two descriptions of a GPU - the
// GPU device's, and the one this side renders on - are one GPU.
struct PciAddress {
    std::uint32_t domain = 0;
    std::uint8_t bus = 0;
    std::uint8_t device = 0;
    std::uint8_t function = 0;

    [[nodiscard]] bool operator==(const PciAddress&) const noexcept = default;

    // "0000:03:00.0", as sysfs names a PCI device; nothing for anything else.
    [[nodiscard]] static std::optional<PciAddress> parse(std::string_view text);
    // The PCI device a DRM node (/dev/dri/renderD128, /dev/dri/card1) is
    // of, through sysfs; nothing for a node that is on no PCI bus.
    [[nodiscard]] static std::optional<PciAddress> of_drm_node(const std::string& node);

    [[nodiscard]] std::string to_string() const;
};

}  // namespace rwin::host
