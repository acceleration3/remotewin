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
    struct AppId {
        std::string aumid;
        std::uint32_t process_id = 0;
        std::string image;

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

    // A window placed against another that does not own it: a menu against
    // the window whose menu it is, a combo box's list against the window it
    // drops from. ``rect`` is where the window sits, relative to the
    // anchor's frame. ``anchor`` none: no such window.
    struct Popup {
        WindowId anchor;
        Rect rect;
        std::uint8_t gravity = 0;
        std::uint8_t constraints = 0;

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
