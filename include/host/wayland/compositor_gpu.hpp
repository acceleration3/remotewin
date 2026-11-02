#pragma once

#include "host/export/pci_address.hpp"

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace rwin::host {

class Globals;

class CompositorGpu {
public:
    explicit CompositorGpu(const Globals& globals);

    [[nodiscard]] const std::optional<PciAddress>& pci() const noexcept { return pci_; }
    [[nodiscard]] const std::string& render_node() const noexcept { return render_node_; }
    [[nodiscard]] bool offers(std::uint32_t fourcc, std::uint64_t modifier) const {
        return formats_.contains({fourcc, modifier});
    }

private:
    std::optional<PciAddress> pci_;
    std::string render_node_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> formats_;
};

}
