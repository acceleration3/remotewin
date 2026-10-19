#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace rwin {

// A named part of the wire and the version of it an end speaks
// (RemoteApp V2's named features, docs/rail-model.md §4.1). Both ends list
// theirs in their hello; a feature is active at the lower version when both
// list it, and every message and window field belongs to one.
struct Feature {
    // Sync*, Window*, ZOrder, Activate, SysCommand, SysMenu, WindowMove,
    // SysParam. The one a link cannot go without.
    static constexpr std::string_view kWindows = "windows";
    static constexpr std::string_view kInput = "input";
    static constexpr std::string_view kFramesPixels = "frames.pixels";
    static constexpr std::string_view kFramesShared = "frames.shared";
    static constexpr std::string_view kCursor = "cursor";
    static constexpr std::string_view kPointerGrab = "pointer.grab";
    static constexpr std::string_view kLatency = "latency";
    static constexpr std::string_view kMoveSize = "movesize";
    // The APPID window field.
    static constexpr std::string_view kAppId = "appid";
    // The POPUP window field.
    static constexpr std::string_view kPopupIntent = "popup.intent";
    static constexpr std::string_view kMonitor = "monitor";

    // Longer names than this are not a peer naming features.
    static constexpr std::size_t kMaxName = 64;

    std::string name;
    std::uint16_t version = 1;

    [[nodiscard]] bool operator==(const Feature&) const = default;
};

}  // namespace rwin
