#include "host/mirror/mirror_role.hpp"

#include "host/chrome/win32_style.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/guest_windows.hpp"
#include "proto/window_fields.hpp"

#include <string_view>

namespace rwin::host {

namespace {

constexpr std::string_view kMenuClass = "#32768";

bool lies_within(const GuestWindow& window, const GuestWindow& parent) {
    const Win32Style style{window};
    if (style.activates() || style.has_caption() || window.fields().class_name == kMenuClass) return false;
    if (window.fields().popup.kind != WindowFields::Popup::Kind::Exact) return false;
    return parent.frame().contains(window.frame());
}

}

MirrorRole MirrorRole::of(const GuestWindow& window, const GuestWindows& windows) {
    const Win32Style style{window};
    WindowId parent = window.anchor();
    if (parent.none() && !window.owner().none()) {
        if (style.stands_alone() && !window.fields().z_glue) return MirrorRole{Kind::Toplevel, window.owner()};
        parent = window.owner();
    }
    const GuestWindow* known = parent.none() || parent == window.id() ? nullptr : windows.find(parent);
    if (known == nullptr) return MirrorRole{};
    if (window.fields().z_glue || lies_within(window, *known)) return MirrorRole{Kind::Subsurface, parent};
    return MirrorRole{Kind::Popup, parent};
}

const char* name_of(MirrorRole::Kind kind) noexcept {
    switch (kind) {
        case MirrorRole::Kind::Toplevel: return "toplevel";
        case MirrorRole::Kind::Popup: return "popup";
        case MirrorRole::Kind::Subsurface: return "subsurface";
    }
    return "?";
}

}
