#include "host/chrome/win32_style.hpp"

#include "host/link/guest_window.hpp"
#include "proto/window_fields.hpp"

namespace rwin::host {

Win32Style::Win32Style(const GuestWindow& window) noexcept
    : words_{.style = window.fields().style.style,
             .extended_style = window.fields().style.extended,
             .class_style = window.fields().style.class_style,
             .layered_flags = window.fields().layering.flags,
             .layered_alpha = window.fields().layering.alpha,
             .color_key = window.fields().layering.color_key,
             .band = window.fields().band,
             .owned = !window.owner().none(),
             .taskbar_button = (window.fields().taskbar & WindowFields::kTaskbarButton) != 0,
             .shaded = (window.fields().frame & WindowFields::kDropShadow) != 0} {}

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
