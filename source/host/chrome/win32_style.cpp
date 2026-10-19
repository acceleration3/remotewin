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

}  // namespace rwin::host
