#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rwin::host {
struct PciAddress {
    std::uint32_t domain = 0;
    std::uint8_t bus = 0;
    std::uint8_t device = 0;
    std::uint8_t function = 0;

    [[nodiscard]] bool operator==(const PciAddress&) const noexcept = default;

    [[nodiscard]] static std::optional<PciAddress> parse(std::string_view text);
    [[nodiscard]] static std::optional<PciAddress> of_drm_node(const std::string& node);

    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] std::string prime_name() const;
};

}
