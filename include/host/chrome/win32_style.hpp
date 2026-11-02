#pragma once

#include <cstdint>

namespace rwin::host {

class GuestWindow;

class Win32Style {
public:
    static constexpr std::uint32_t kWsDisabled = 0x08000000;
    static constexpr std::uint32_t kWsBorder = 0x00800000;
    static constexpr std::uint32_t kWsDlgFrame = 0x00400000;
    static constexpr std::uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
    static constexpr std::uint32_t kWsPopup = 0x80000000;
    static constexpr std::uint32_t kWsSysMenu = 0x00080000;
    static constexpr std::uint32_t kWsThickFrame = 0x00040000;
    static constexpr std::uint32_t kWsMinimizeBox = 0x00020000;
    static constexpr std::uint32_t kWsMaximizeBox = 0x00010000;
    static constexpr std::uint32_t kWsExTopMost = 0x00000008;
    static constexpr std::uint32_t kWsExTransparent = 0x00000020;
    static constexpr std::uint32_t kWsExToolWindow = 0x00000080;
    static constexpr std::uint32_t kWsExContextHelp = 0x00000400;
    static constexpr std::uint32_t kWsExAppWindow = 0x00040000;
    static constexpr std::uint32_t kWsExLayered = 0x00080000;
    static constexpr std::uint32_t kWsExNoActivate = 0x08000000;
    static constexpr std::uint32_t kCsNoClose = 0x00000200;
    static constexpr std::uint32_t kCsDropShadow = 0x00020000;
    static constexpr std::uint32_t kLwaColorKey = 0x00000001;
    static constexpr std::uint32_t kLwaAlpha = 0x00000002;
    static constexpr std::uint32_t kBandDesktop = 1;

    struct Words {
        std::uint32_t style = 0;
        std::uint32_t extended_style = 0;
        std::uint32_t class_style = 0;
        std::uint32_t layered_flags = 0;
        std::uint8_t layered_alpha = 255;
        std::uint32_t color_key = 0;
        std::uint32_t band = kBandDesktop;
        bool owned = false;
        bool taskbar_button = false;
        bool in_switchers = false;
        bool shaded = false;

        [[nodiscard]] constexpr bool operator==(const Words&) const noexcept = default;
    };

    constexpr explicit Win32Style(Words words) noexcept : words_(words) {}
    explicit Win32Style(const GuestWindow& window) noexcept;

    [[nodiscard]] constexpr bool operator==(const Win32Style&) const noexcept = default;

    [[nodiscard]] constexpr bool has_caption() const noexcept { return has(kWsCaption); }

    [[nodiscard]] constexpr bool has_system_menu() const noexcept {
        return has(kWsSysMenu);
    }

    [[nodiscard]] constexpr bool has_minimize_box() const noexcept {
        return has(kWsMinimizeBox);
    }

    [[nodiscard]] constexpr bool has_maximize_box() const noexcept {
        return has(kWsMaximizeBox);
    }

    [[nodiscard]] constexpr bool sizable() const noexcept { return has(kWsThickFrame); }

    [[nodiscard]] constexpr bool disabled() const noexcept { return has(kWsDisabled); }

    [[nodiscard]] constexpr bool closable() const noexcept { return !has_class(kCsNoClose); }

    [[nodiscard]] constexpr bool topmost() const noexcept {
        return has_ex(kWsExTopMost) || words_.band > kBandDesktop;
    }

    [[nodiscard]] constexpr bool tool_window() const noexcept {
        return has_ex(kWsExToolWindow);
    }

    [[nodiscard]] constexpr bool activates() const noexcept {
        return !has_ex(kWsExNoActivate);
    }

    [[nodiscard]] constexpr bool click_through() const noexcept {
        return has_ex(kWsExLayered) && has_ex(kWsExTransparent);
    }

    [[nodiscard]] constexpr bool casts_shadow() const noexcept { return words_.shaded; }

    [[nodiscard]] constexpr std::uint8_t opacity() const noexcept {
        return (words_.layered_flags & kLwaAlpha) != 0 ? words_.layered_alpha : 255;
    }

    [[nodiscard]] constexpr bool color_keyed() const noexcept {
        return (words_.layered_flags & kLwaColorKey) != 0;
    }

    [[nodiscard]] constexpr std::uint32_t color_key() const noexcept {
        return words_.color_key;
    }

    [[nodiscard]] constexpr bool owned() const noexcept { return words_.owned; }

    [[nodiscard]] constexpr bool stands_alone() const noexcept {
        if (!words_.owned || !activates()) return false;
        return has_caption() || !has(kWsPopup);
    }

    [[nodiscard]] constexpr bool in_taskbar() const noexcept { return words_.taskbar_button; }

    [[nodiscard]] constexpr bool off_taskbar() const noexcept { return !words_.owned && !words_.taskbar_button; }
    [[nodiscard]] constexpr bool off_switcher() const noexcept { return !words_.owned && !words_.in_switchers; }

private:
    [[nodiscard]] constexpr bool has(std::uint32_t bit) const noexcept {
        return (words_.style & bit) == bit;
    }
    [[nodiscard]] constexpr bool has_ex(std::uint32_t bit) const noexcept {
        return (words_.extended_style & bit) == bit;
    }
    [[nodiscard]] constexpr bool has_class(std::uint32_t bit) const noexcept {
        return (words_.class_style & bit) == bit;
    }

    Words words_;
};

}
