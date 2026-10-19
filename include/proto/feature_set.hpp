#pragma once

#include "proto/feature.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rwin {

// The features active on a link: those both hellos list, each at the lower
// of the two versions. Before both hellos, nothing is.
class FeatureSet {
public:
    FeatureSet() = default;
    FeatureSet(const std::vector<Feature>& ours, const std::vector<Feature>& theirs);

    [[nodiscard]] bool has(std::string_view name) const noexcept { return version(name) != 0; }
    // 0 when the feature is not active.
    [[nodiscard]] std::uint16_t version(std::string_view name) const noexcept;

    // What the peer lacks of what a link needs ("windows"), as one line for
    // the log; empty when the link can go on. Anything else missing only
    // makes a window do less.
    [[nodiscard]] std::string refusal() const;

    // "name@version, ..." for the log.
    [[nodiscard]] std::string describe() const;

private:
    std::vector<Feature> active_;
};

}  // namespace rwin
