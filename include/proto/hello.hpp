#pragma once

#include "proto/feature.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct Hello {
    static constexpr std::size_t kMaxFeatures = 64;

    std::string build;
    std::vector<Feature> features;

    [[nodiscard]] static Hello ours();

    [[nodiscard]] bool operator==(const Hello&) const = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<Hello> read(MessageReader& in);
};

}
