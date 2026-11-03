#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rwin {
struct Feature {
    static constexpr std::string_view kWindows = "windows";
    static constexpr std::string_view kInput = "input";
    static constexpr std::string_view kFramesPixels = "frames.pixels";
    static constexpr std::string_view kFramesShared = "frames.shared";
    static constexpr std::string_view kCursor = "cursor";
    static constexpr std::string_view kPointerGrab = "pointer.grab";
    static constexpr std::string_view kCursorAt = "cursor.at";
    static constexpr std::string_view kLatency = "latency";
    static constexpr std::string_view kMoveSize = "movesize";
    static constexpr std::string_view kAppId = "appid";
    static constexpr std::uint16_t kAppIdVersion = 2;
    static constexpr std::string_view kPopupIntent = "popup.intent";
    static constexpr std::string_view kIcons = "icons";
    static constexpr std::string_view kMonitor = "monitor";

    static constexpr std::size_t kMaxName = 64;

    std::string name;
    std::uint16_t version = 1;

    [[nodiscard]] bool operator==(const Feature&) const = default;
};

}
