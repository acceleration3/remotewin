#pragma once

#include "proto/feature.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rwin {
class FeatureSet {
public:
    FeatureSet() = default;
    FeatureSet(const std::vector<Feature>& ours, const std::vector<Feature>& theirs);

    [[nodiscard]] bool has(std::string_view name) const noexcept { return version(name) != 0; }
    [[nodiscard]] std::uint16_t version(std::string_view name) const noexcept;

    [[nodiscard]] std::string refusal() const;

    [[nodiscard]] std::string describe() const;

private:
    std::vector<Feature> active_;
};

}
