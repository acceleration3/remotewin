#include "proto/feature_set.hpp"

#include <algorithm>

namespace rwin {

FeatureSet::FeatureSet(const std::vector<Feature>& ours, const std::vector<Feature>& theirs) {
    for (const Feature& mine : ours) {
        const auto matched = std::find_if(theirs.begin(), theirs.end(),
                                          [&](const Feature& other) { return other.name == mine.name; });
        if (matched == theirs.end()) continue;
        active_.push_back(Feature{mine.name, std::min(mine.version, matched->version)});
    }
}

std::uint16_t FeatureSet::version(std::string_view name) const noexcept {
    const auto found =
        std::find_if(active_.begin(), active_.end(), [&](const Feature& feature) { return feature.name == name; });
    return found == active_.end() ? 0 : found->version;
}

std::string FeatureSet::refusal() const {
    if (has(Feature::kWindows)) return {};
    return "the other half does not speak \"" + std::string(Feature::kWindows)
           + "\", which a mirror needs: it is from another build. Put the halves built together on both sides";
}

std::string FeatureSet::describe() const {
    std::string line;
    for (const Feature& feature : active_) {
        if (!line.empty()) line += ", ";
        line += feature.name + "@" + std::to_string(feature.version);
    }
    return line.empty() ? std::string("none") : line;
}

}  // namespace rwin
