#pragma once

#include "proto/feature.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

// The first message each end sends on a mirror connection (HostHello,
// GuestHello): which build it is, for the log, and which features it
// speaks (RAIL's Handshake and capability sets). The build is never
// compared; the features are (FeatureSet).
struct Hello {
    static constexpr std::size_t kMaxFeatures = 64;

    std::string build;
    std::vector<Feature> features;

    // What this build speaks: both halves are built from this list.
    [[nodiscard]] static Hello ours();

    [[nodiscard]] bool operator==(const Hello&) const = default;

    void write(MessageWriter& out) const;
    // Refused when it lists more features than kMaxFeatures, a name longer
    // than Feature::kMaxName, or a feature at version 0.
    [[nodiscard]] static std::optional<Hello> read(MessageReader& in);
};

}  // namespace rwin
