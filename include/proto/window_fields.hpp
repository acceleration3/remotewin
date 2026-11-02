#pragma once

#include "proto/rect.hpp"
#include "proto/window_id.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rwin {

class MessageReader;
class MessageWriter;

struct WindowFields {
    enum Field : std::uint32_t {
        kOwner = 1u << 0,
        kStyle = 1u << 1,
        kShow = 1u << 2,
        kTitle = 1u << 3,
        kGeometry = 1u << 4,
        kTaskbar = 1u << 5,
        kZGlue = 1u << 6,
        kAppId = 1u << 7,
        kClass = 1u << 8,
        kLayering = 1u << 9,
        kBand = 1u << 10,
        kFrame = 1u << 11,
        kPopup = 1u << 12,
        kDpi = 1u << 13,
        kCloaked = 1u << 14,
    };
    static constexpr std::uint32_t kAll = (1u << 15) - 1;

    enum class Show : std::uint8_t { Hidden = 0, Minimized = 2, Maximized = 3, Shown = 5 };

    static constexpr std::uint8_t kTaskbarButton = 1;
    static constexpr std::uint8_t kInSwitchers = 2;
    static constexpr std::uint8_t kDwmFrame = 1;
    static constexpr std::uint8_t kDropShadow = 2;

    static constexpr std::size_t kMaxRegionRects = 256;

    struct Style {
        static constexpr std::uint32_t kRailStyle = 0xC80F0000;
        static constexpr std::uint32_t kRailExtended = 0x080C3088;
        static constexpr std::uint32_t kCaption = 0x00C00000;
        static constexpr std::uint32_t kTransparent = 0x00000020;
        static constexpr std::uint32_t kContextHelp = 0x00000400;
        static constexpr std::uint32_t kLayered = 0x00080000;
        static constexpr std::uint32_t kStyleMask = kRailStyle | kCaption;
        static constexpr std::uint32_t kExtendedMask = kRailExtended | kTransparent | kContextHelp;

        std::uint32_t style = 0;
        std::uint32_t extended = 0;
        std::uint32_t class_style = 0;

        [[nodiscard]] bool operator==(const Style&) const noexcept = default;
    };

    struct Geometry {
        Rect frame;
        Rect client;
        std::uint16_t margin_left = 0;
        std::uint16_t margin_top = 0;
        std::uint16_t margin_right = 0;
        std::uint16_t margin_bottom = 0;
        std::vector<Rect> region;
        Rect picture;

        [[nodiscard]] bool operator==(const Geometry&) const = default;
    };

    struct ProcessRef {
        std::uint32_t pid = 0;
        std::uint64_t created = 0;

        [[nodiscard]] bool operator==(const ProcessRef&) const noexcept = default;
    };

    struct AppId {
        static constexpr std::size_t kMostTree = 16;

        std::string aumid;
        std::uint32_t process_id = 0;
        std::string image;
        std::vector<ProcessRef> tree;

        [[nodiscard]] bool operator==(const AppId&) const = default;
    };

    struct Layering {
        std::uint32_t flags = 0;
        std::uint8_t alpha = 255;
        std::uint32_t color_key = 0;

        [[nodiscard]] bool operator==(const Layering&) const noexcept = default;
    };

    struct Popup {
        enum class Kind : std::uint8_t { Exact = 0, MenuBar = 1, Submenu = 2, Combo = 3, Context = 4, Tooltip = 5 };
        enum class Edge : std::uint8_t {
            None = 0,
            Top = 1,
            Bottom = 2,
            Left = 3,
            Right = 4,
            TopLeft = 5,
            BottomLeft = 6,
            TopRight = 7,
            BottomRight = 8,
        };
        static constexpr std::uint8_t kSlideX = 1;
        static constexpr std::uint8_t kSlideY = 2;
        static constexpr std::uint8_t kFlipX = 4;
        static constexpr std::uint8_t kFlipY = 8;

        WindowId anchor;
        Kind kind = Kind::Exact;
        Rect placed;
        Rect opens_from;
        Edge edge = Edge::None;
        Edge gravity = Edge::None;
        std::uint8_t adjust = 0;

        [[nodiscard]] bool operator==(const Popup&) const noexcept = default;
    };

    WindowId owner;
    Style style;
    Show show = Show::Hidden;
    std::string title;
    Geometry geometry;
    std::uint8_t taskbar = 0;
    bool z_glue = false;
    AppId app_id;
    std::string class_name;
    Layering layering;
    std::uint32_t band = 1;
    std::uint8_t frame = 0;
    Popup popup;
    std::uint16_t dpi = 96;
    bool cloaked = false;

    [[nodiscard]] bool operator==(const WindowFields&) const = default;

    [[nodiscard]] std::uint32_t differences(const WindowFields& other) const;

    void write(MessageWriter& out, std::uint32_t mask) const;
    [[nodiscard]] static bool read(MessageReader& in, WindowFields& into, std::uint32_t& mask);
};

}
