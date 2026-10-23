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

// Everything the guest says about one window, field by field
// (docs/rail-model.md §4.2, RAIL's Window Information order). WindowNew
// carries every field its link's features allow; WindowChanged carries
// only the fields that differ from what was last sent, named by a mask.
//
// The fields are what Windows keeps about the window and acts on, read
// as RAIL's server reads them. Presentation - popup or toplevel, taskbar
// or not, which edges resize - is the host's to decide from them.
struct WindowFields {
    enum Field : std::uint32_t {
        kOwner = 1u << 0,
        kStyle = 1u << 1,
        kShow = 1u << 2,
        kTitle = 1u << 3,
        // Sent whole: a frame that moved and a region that did not are one
        // change, as RAIL sends its geometry all or nothing.
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

    // RAIL's ShowState values.
    enum class Show : std::uint8_t { Hidden = 0, Minimized = 2, Maximized = 3, Shown = 5 };

    // TASKBAR bits: Windows' own taskbar rule, worked out in the guest.
    static constexpr std::uint8_t kTaskbarButton = 1;
    static constexpr std::uint8_t kInSwitchers = 2;
    // FRAME bits: DWM draws the non-client frame; Windows shades the window.
    static constexpr std::uint8_t kDwmFrame = 1;
    static constexpr std::uint8_t kDropShadow = 2;

    // A region of more rectangles than this is a mask no shape needs - an
    // antialiased edge scanned into rows - and the window is its frame.
    static constexpr std::size_t kMaxRegionRects = 256;

    // GWL_STYLE and GWL_EXSTYLE as RAIL masks them, and GCL_STYLE.
    //
    // RAIL sends style & 0xC80F0000 (popup, child, disabled, system menu,
    // sizing border, minimise and maximise boxes) and ex-style & 0x080C3088
    // (no-activate, layered, app window, RTL reading, right, tool window,
    // topmost); visibility, minimised and maximised travel as SHOW. Three
    // bits more go here because the host's chrome reads them and RAIL's
    // client draws none: WS_CAPTION, WS_EX_TRANSPARENT, WS_EX_CONTEXTHELP.
    // A window the taskbar rule keeps off the taskbar is sent with
    // WS_EX_TOOLWINDOW forced on, as RAIL sends it.
    struct Style {
        static constexpr std::uint32_t kRailStyle = 0xC80F0000;
        static constexpr std::uint32_t kRailExtended = 0x080C3088;
        static constexpr std::uint32_t kCaption = 0x00C00000;
        static constexpr std::uint32_t kTransparent = 0x00000020;
        static constexpr std::uint32_t kContextHelp = 0x00000400;
        // WS_EX_LAYERED, one of RAIL's: the window's alpha is its own.
        static constexpr std::uint32_t kLayered = 0x00080000;
        static constexpr std::uint32_t kStyleMask = kRailStyle | kCaption;
        static constexpr std::uint32_t kExtendedMask = kRailExtended | kTransparent | kContextHelp;

        std::uint32_t style = 0;
        std::uint32_t extended = 0;
        std::uint32_t class_style = 0;

        [[nodiscard]] bool operator==(const Style&) const noexcept = default;
    };

    // In guest physical pixels. ``frame`` is on the guest's screen, the DWM
    // extended frame bounds; everything else is relative to it. ``margins``
    // are the invisible resize borders outside the frame. ``region`` is the
    // window's own shape, empty when it is its frame. ``picture`` is where
    // the frames the host is sent lie: the frame itself, unless the window
    // leaves a transparent margin it does not draw, which is trimmed away.
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

    // The taskbar's identity for the window (IApplicationResolver), and the
    // process it runs in.
    // A process as Windows tells it from another that was given its pid
    // later: the pid and when it was created (FILETIME ticks).
    struct ProcessRef {
        std::uint32_t pid = 0;
        std::uint64_t created = 0;

        [[nodiscard]] bool operator==(const ProcessRef&) const noexcept = default;
    };

    struct AppId {
        // Most processes of ``tree``: a program's tree is not deeper.
        static constexpr std::size_t kMostTree = 16;

        std::string aumid;
        std::uint32_t process_id = 0;
        std::string image;
        // The process, then its parents nearest first (each verified a parent
        // by creation time), up to the root of the
        // program's process tree - the topmost process that is not the
        // session's own (explorer, a service host, a broker): the pids a
        // window of any process of a launched program has in common with the
        // program's. Empty where the guest could not read the process.
        std::vector<ProcessRef> tree;

        [[nodiscard]] bool operator==(const AppId&) const = default;
    };

    // SetLayeredWindowAttributes: LWA_ALPHA and/or LWA_COLORKEY, the
    // constant alpha, and the keyed colour as a COLORREF (0x00BBGGRR).
    struct Layering {
        std::uint32_t flags = 0;
        std::uint8_t alpha = 255;
        std::uint32_t color_key = 0;

        [[nodiscard]] bool operator==(const Layering&) const noexcept = default;
    };

    // A window placed against another: a menu against the window whose menu
    // bar it drops from or the menu it opens out of, a combo box's list
    // against the box, a tooltip or a context menu against the window under
    // the pointer, any other popup against its owner. ``placed`` is where
    // Windows put it, relative to the anchor's frame. The rest is where it
    // was meant to open before Windows flipped or slid it against a guest
    // screen that is not the one it is shown on (R2, popups by intent): from
    // ``opens_from`` on the anchor's frame, at its ``edge``, towards
    // ``gravity``, giving way at a screen's edge as ``adjust`` allows. Edge,
    // gravity and adjustment are xdg_positioner's own values, which are
    // what a compositor places a popup by.
    struct Popup {
        enum class Kind : std::uint8_t { Exact = 0, MenuBar = 1, Submenu = 2, Combo = 3, Context = 4, Tooltip = 5 };
        // xdg_positioner.anchor, and .gravity, which numbers the same.
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
        // xdg_positioner.constraint_adjustment.
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

    // GW_OWNER as RAIL sends it: none, a window, or WindowId::hidden_owner.
    WindowId owner;
    Style style;
    Show show = Show::Hidden;
    std::string title;
    Geometry geometry;
    std::uint8_t taskbar = 0;
    // Kept next to its neighbours in the z-order (RAIL's
    // EnforceServerZOrder): Office's frame-shadow windows.
    bool z_glue = false;
    AppId app_id;
    std::string class_name;
    Layering layering;
    // GetWindowBand: 1 is the desktop's, where every ordinary window is.
    std::uint32_t band = 1;
    std::uint8_t frame = 0;
    Popup popup;
    std::uint16_t dpi = 96;
    bool cloaked = false;

    [[nodiscard]] bool operator==(const WindowFields&) const = default;

    // The fields in which ``other`` says something else.
    [[nodiscard]] std::uint32_t differences(const WindowFields& other) const;

    // ``mask``, then the fields it names in bit order.
    void write(MessageWriter& out, std::uint32_t mask) const;
    // The mask and the fields it names, over what ``into`` held. False for a
    // mask naming fields past kAll, a show state RAIL has no value for, a
    // region past kMaxRegionRects or anything cut short.
    [[nodiscard]] static bool read(MessageReader& in, WindowFields& into, std::uint32_t& mask);
};

}  // namespace rwin
