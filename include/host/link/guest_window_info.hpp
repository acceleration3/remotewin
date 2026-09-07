#pragma once

#include <QList>
#include <QRect>
#include <QString>

#include <cstdint>

namespace rwin::host {

// One guest window as it arrived on the wire: every word Windows keeps
// about the window and acts on, carried as read. These fields always
// travel together, so they are one thing.
struct GuestWindowInfo {
    std::uint64_t handle = 0;
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t style = 0;           // GWL_STYLE
    std::uint32_t extended_style = 0;  // GWL_EXSTYLE
    std::uint32_t class_style = 0;     // GCL_STYLE
    QString title;
    QString class_name;
    bool minimized = false;
    bool maximized = false;
    // DWMWA_NCRENDERING_ENABLED: DWM draws the frame, shadow included.
    bool dwm_frame = false;
    bool foreground = false;
    // SetLayeredWindowAttributes: LWA_ALPHA / LWA_COLORKEY, the constant
    // alpha, and the keyed colour as a COLORREF (0x00BBGGRR).
    std::uint32_t layered_flags = 0;
    std::uint8_t layered_alpha = 255;
    std::uint32_t color_key = 0;
    // The z-order band; 1 is the desktop, where every ordinary window is.
    std::uint32_t band = 0;
    // SetWindowRgn, in the window's own coordinates. Empty: the window is
    // its whole rectangle.
    QList<QRect> region;
};

}  // namespace rwin::host
