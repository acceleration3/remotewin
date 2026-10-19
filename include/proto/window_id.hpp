#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

// A guest window as the wire names it: its HWND and the serial the guest
// gave it when it admitted that HWND. Windows hands a destroyed window's
// handle to the next window made, and a message about the old one must
// not reach the new one; RAIL holds destroyed ids back for a while,
// remotewin numbers them instead. HWNDs are 32 bits wide on 64-bit
// Windows too.
struct WindowId {
    std::uint32_t hwnd = 0;
    std::uint32_t serial = 0;

    // The owner of a window whose owner is hidden, empty or minimised to
    // nothing (RAIL's 0xFFFFFFFE): a window of its own, with a taskbar
    // button, rather than a part of something the host never sees.
    static constexpr std::uint32_t kHiddenOwnerHwnd = 0xFFFFFFFE;

    [[nodiscard]] static constexpr WindowId hidden_owner() noexcept { return WindowId{kHiddenOwnerHwnd, 0}; }

    [[nodiscard]] constexpr bool none() const noexcept { return hwnd == 0 && serial == 0; }
    [[nodiscard]] constexpr bool is_hidden_owner() const noexcept { return *this == hidden_owner(); }

    // One number for a map key: the two fields as they cross the wire.
    [[nodiscard]] constexpr std::uint64_t key() const noexcept {
        return (static_cast<std::uint64_t>(hwnd) << 32) | serial;
    }
    [[nodiscard]] static constexpr WindowId of_key(std::uint64_t key) noexcept {
        return WindowId{static_cast<std::uint32_t>(key >> 32), static_cast<std::uint32_t>(key)};
    }

    [[nodiscard]] constexpr bool operator==(const WindowId&) const noexcept = default;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WindowId> read(MessageReader& in);
};

// What a mirror of the whole monitor's frames and input name
// (WatchMonitor), and the marker in a ZOrder list. No admitted window has
// serial 0, so neither is ever read as a window.
inline constexpr WindowId kMonitorWindow{};

}  // namespace rwin

template <>
struct std::hash<rwin::WindowId> {
    std::size_t operator()(const rwin::WindowId& id) const noexcept { return std::hash<std::uint64_t>{}(id.key()); }
};

// "0x30a2c:7", hwnd in hex and serial: how logs on both ends name a window.
template <>
struct std::formatter<rwin::WindowId> : std::formatter<std::string_view> {
    auto format(const rwin::WindowId& id, std::format_context& context) const {
        return std::format_to(context.out(), "{:#x}:{}", id.hwnd, id.serial);
    }
};
