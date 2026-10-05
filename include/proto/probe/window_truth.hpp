#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace rwin {

class MessageReader;
class MessageWriter;

// What Windows says a probe window is, read back from Windows after it was
// made: the words as Windows normalised them, the rectangles it keeps, and
// the state it is in. This is the side of a comparison that is right by
// definition, so nothing here is derived from what was asked for.
struct WindowTruth {
    // Screen coordinates in physical pixels, right and bottom exclusive.
    struct Rect {
        std::int32_t left = 0;
        std::int32_t top = 0;
        std::int32_t right = 0;
        std::int32_t bottom = 0;

        [[nodiscard]] std::int32_t width() const noexcept { return right - left; }
        [[nodiscard]] std::int32_t height() const noexcept { return bottom - top; }
        [[nodiscard]] bool operator==(const Rect&) const noexcept = default;
    };

    // GetWindowRgn's answer.
    enum class RegionKind : std::uint8_t { None, Simple, Complex };

    std::uint64_t handle = 0;
    // IsWindow: false once Windows destroyed it - closed, or taken down
    // with its owner. Nothing else in a truth of a window that is gone is
    // read.
    bool exists = true;
    // GW_OWNER.
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    std::uint32_t style = 0;           // GWL_STYLE
    std::uint32_t extended_style = 0;  // GWL_EXSTYLE
    std::uint32_t class_style = 0;     // GCL_STYLE
    std::string title;
    std::string class_name;
    // GetWindowRect: the invisible resize border included.
    Rect window;
    // DWMWA_EXTENDED_FRAME_BOUNDS: what is drawn. The window rectangle
    // when DWM has no frame for it.
    Rect visible;
    Rect client;
    bool shown = false;      // IsWindowVisible
    bool iconic = false;     // IsIconic
    bool zoomed = false;     // IsZoomed
    bool enabled = false;    // IsWindowEnabled
    bool foreground = false; // GetForegroundWindow
    bool active = false;     // GetActiveWindow, on the probe's one thread
    // GetFocus is this window or a child of it.
    bool focused = false;
    // Some window up its owner chain is disabled: what a modal dialog
    // does to the window it was opened for.
    bool owner_disabled = false;
    // GW_ENABLEDPOPUP: the enabled window it owns that Windows activates
    // when it is clicked while disabled; 0 for none.
    std::uint64_t enabled_popup = 0;
    // Where it is in the z-order among the probe's own windows, 0 the
    // topmost; -1 when it is not in the z-order at all.
    std::int32_t z_rank = -1;
    // DWMWA_NCRENDERING_ENABLED: DWM draws this window's frame.
    bool dwm_frame = false;
    std::uint32_t dpi = 0;  // GetDpiForWindow
    // GetLayeredWindowAttributes; zero flags for a window that is not
    // layered or never set them.
    std::uint32_t layered_flags = 0;
    std::uint8_t layered_alpha = 255;
    std::uint32_t color_key = 0;
    RegionKind region = RegionKind::None;
    // How many rectangles the region is made of, and its bounding box,
    // in the window's own coordinates.
    std::uint32_t region_rects = 0;
    Rect region_box;

    void write(MessageWriter& out) const;
    [[nodiscard]] static std::optional<WindowTruth> read(MessageReader& in);
};

}  // namespace rwin
