#include "proto/hello.hpp"

#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"

#include <string_view>
#include <utility>

namespace rwin {

namespace {

constexpr std::string_view kBuild = "remotewin wire 7";

}

Hello Hello::ours() {
    Hello hello;
    hello.build = std::string(kBuild);
    for (const std::string_view name :
         {Feature::kWindows, Feature::kInput, Feature::kFramesPixels, Feature::kFramesShared, Feature::kCursor,
          Feature::kPointerGrab, Feature::kLatency, Feature::kMoveSize, Feature::kAppId, Feature::kMonitor,
          Feature::kIcons}) {
        hello.features.push_back(Feature{std::string(name), name == Feature::kAppId ? Feature::kAppIdVersion : std::uint16_t{1}});
    }
    hello.features.push_back(Feature{std::string(Feature::kPopupIntent), 2});
    return hello;
}

void Hello::write(MessageWriter& out) const {
    out.put_string(build);
    out.put_u16(static_cast<std::uint16_t>(features.size()));
    for (const Feature& feature : features) {
        out.put_string(feature.name);
        out.put_u16(feature.version);
    }
}

std::optional<Hello> Hello::read(MessageReader& in) {
    Hello hello;
    hello.build = in.string();
    const std::uint16_t count = in.u16();
    if (!in.ok() || count > kMaxFeatures) return std::nullopt;
    hello.features.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        Feature feature;
        feature.name = in.string();
        feature.version = in.u16();
        if (!in.ok() || feature.name.empty() || feature.name.size() > Feature::kMaxName || feature.version == 0) {
            return std::nullopt;
        }
        hello.features.push_back(std::move(feature));
    }
    return hello;
}

}
