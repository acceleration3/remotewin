#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rwin::guest {

// A rectangle in a window's picture's coordinates (WindowSurface).
struct WindowRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    [[nodiscard]] bool operator==(const WindowRect&) const noexcept = default;
};

// One top-level window as Windows describes it.
//
// Every word here is one Windows keeps about the window and acts on — the
// style words, who owns it, what DWM does to its frame, how a layered
// window is blended, the shape it was given, the band it stacks in, and
// whether it is the foreground. They are carried as read, never
// interpreted on this side: the host makes a mirror behave the way these
// words say the window behaves.
struct GuestWindow {
    std::uint64_t handle = 0;
    // The window this one belongs to, if any. Windows keeps an owned
    // window above its owner and takes it down with it.
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    // The picture's place and size on the screen (WindowSurface), not the
    // window rectangle's.
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t style = 0;           // GWL_STYLE
    std::uint32_t extended_style = 0;  // GWL_EXSTYLE
    std::uint32_t class_style = 0;     // GCL_STYLE
    std::string title;
    std::string class_name;
    bool minimized = false;
    bool maximized = false;
    // DWMWA_NCRENDERING_ENABLED: DWM draws this window's frame, and with
    // it the frame's shadow.
    bool dwm_frame = false;
    // SetLayeredWindowAttributes, when the window used it: LWA_ALPHA
    // and/or LWA_COLORKEY, the constant alpha, and the keyed colour as a
    // COLORREF.
    std::uint32_t layered_flags = 0;
    std::uint8_t layered_alpha = 255;
    std::uint32_t color_key = 0;
    // The z-order band (GetWindowBand). Every ordinary window is in the
    // desktop band; a higher band stacks above all of them, topmost or
    // not.
    std::uint32_t band = 0;
    bool foreground = false;
    // SetWindowRgn, as the rectangles of the region, in the picture's
    // coordinates. Empty when the window has none and is its whole
    // rectangle.
    std::vector<WindowRect> region;
};

// Every top-level window worth showing.
//
// "Visible" is not enough. Windows keeps windows that are mapped and
// sized and report themselves visible, but which the compositor is not
// drawing — cloaked, in DWM's word. A console has one of these beside
// its real window, identical in class, size and style, and mirroring it
// puts up a second empty copy of the same terminal.
class ApplicationProcess;
class LaunchScope;

class WindowEnumerator {
public:
    explicit WindowEnumerator(ApplicationProcess& applications) : applications_(applications) {}

    // Narrowed to the application ``scope`` describes.
    [[nodiscard]] std::vector<GuestWindow> windows_in(const LaunchScope& scope);

private:
    ApplicationProcess& applications_;
};

}  // namespace rwin::guest
