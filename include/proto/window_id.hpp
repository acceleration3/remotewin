#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowId {
    std::uint32_t hwnd = 0;
    std::uint32_t serial = 0;

    static constexpr std::uint32_t kHiddenOwnerHwnd = 0xFFFFFFFE;

    [[nodiscard]] static constexpr WindowId hidden_owner() noexcept { return WindowId{kHiddenOwnerHwnd, 0}; }

    [[nodiscard]] constexpr bool none() const noexcept { return hwnd == 0 && serial == 0; }
    [[nodiscard]] constexpr bool is_hidden_owner() const noexcept { return *this == hidden_owner(); }

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

inline constexpr WindowId kMonitorWindow{};

}

template <>
struct std::hash<rwin::WindowId> {
    std::size_t operator()(const rwin::WindowId& id) const noexcept { return std::hash<std::uint64_t>{}(id.key()); }
};

template <>
struct std::formatter<rwin::WindowId> : std::formatter<std::string_view> {
    auto format(const rwin::WindowId& id, std::format_context& context) const {
        return std::format_to(context.out(), "{:#x}:{}", id.hwnd, id.serial);
    }
};
