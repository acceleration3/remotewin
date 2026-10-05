#include "host/chrome/win32_style.hpp"

#include "host/link/guest_window_info.hpp"

namespace rwin::host {

Win32Style::Win32Style(const GuestWindowInfo& window) noexcept
    : words_{.style = window.style,
             .extended_style = window.extended_style,
             .class_style = window.class_style,
             .layered_flags = window.layered_flags,
             .layered_alpha = window.layered_alpha,
             .color_key = window.color_key,
             .band = window.band,
             .dwm_frame = window.dwm_frame,
             .owned = window.owner != 0,
             .desktop_shadows = window.drop_shadows} {}

Qt::WindowFlags Win32Style::hints() const noexcept {
    Qt::WindowFlags flags = Qt::FramelessWindowHint;
    if (has_caption()) flags |= Qt::WindowTitleHint;
    if (has_system_menu()) flags |= Qt::WindowSystemMenuHint;
    if (has_system_menu() && closable()) flags |= Qt::WindowCloseButtonHint;
    if (has_minimize_box()) flags |= Qt::WindowMinimizeButtonHint;
    if (has_maximize_box()) flags |= Qt::WindowMaximizeButtonHint;
    if (has_ex(kWsExContextHelp)) flags |= Qt::WindowContextHelpButtonHint;
    if (topmost()) flags |= Qt::WindowStaysOnTopHint;
    if (!activates()) flags |= Qt::WindowDoesNotAcceptFocus;
    if (click_through()) flags |= Qt::WindowTransparentForInput;
    // The compositor would shade anything; a shadow Windows never drew is
    // a halo around the window.
    if (!casts_shadow()) flags |= Qt::NoDropShadowWindowHint;
    return flags;
}

Qt::WindowType Win32Style::toplevel_type() const noexcept {
    // Qt::Tool is the toplevel Qt makes transient to whatever is in
    // front, which this compositor keeps above that and off the taskbar.
    return in_taskbar() ? Qt::Window : Qt::Tool;
}

}  // namespace rwin::host
