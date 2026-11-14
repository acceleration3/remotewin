#include "host/mirror/blank_picture.hpp"

#include "host/link/guest_window.hpp"
#include "host/mirror/mirror_role.hpp"

#include <cstdio>
#include <optional>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

using rwin::Rect;
using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::blank_picture;
using rwin::host::GuestWindow;
using rwin::host::MirrorRole;
using rwin::host::Size;
using Kind = rwin::host::MirrorRole::Kind;

constexpr Size kIconic{183, 26};
constexpr Size kNormal{1280, 800};

GuestWindow window(WindowFields::Show show, Size picture) {
    WindowFields fields;
    fields.show = show;
    fields.geometry.frame = Rect{-32000, -32000, static_cast<std::uint32_t>(picture.width),
                                 static_cast<std::uint32_t>(picture.height)};
    fields.geometry.picture = Rect{0, 0, static_cast<std::uint32_t>(picture.width),
                                   static_cast<std::uint32_t>(picture.height)};
    return GuestWindow{WindowId{0x9605e4, 1}, fields};
}

MirrorRole role(Kind kind) { return MirrorRole{kind, WindowId{}}; }

void minimized_before_any_picture() {
    const GuestWindow iconic = window(WindowFields::Show::Minimized, kIconic);
    check(blank_picture(iconic, role(Kind::Toplevel), kIconic, false) == kIconic,
          "a toplevel minimised before its first picture is blank, at the size the guest says it has");
    check(blank_picture(iconic, role(Kind::Toplevel), kNormal, false) == kNormal,
          "minimised after it was seen at a size but before a picture, it is blank at that size");
}

void pictured_or_not_minimized() {
    const GuestWindow iconic = window(WindowFields::Show::Minimized, kIconic);
    check(!blank_picture(iconic, role(Kind::Toplevel), kNormal, true).has_value(),
          "a minimised window the guest pictured keeps its picture");
    for (const WindowFields::Show show :
         {WindowFields::Show::Shown, WindowFields::Show::Maximized, WindowFields::Show::Hidden}) {
        check(!blank_picture(window(show, kNormal), role(Kind::Toplevel), kNormal, false).has_value(),
              "a window that is not minimised waits for its picture");
    }
}

void only_toplevels() {
    const GuestWindow iconic = window(WindowFields::Show::Minimized, kIconic);
    check(!blank_picture(iconic, role(Kind::Popup), kIconic, false).has_value(), "a popup is never blank");
    check(!blank_picture(iconic, role(Kind::Subsurface), kIconic, false).has_value(), "a part is never blank");
}

void no_size() {
    const GuestWindow iconic = window(WindowFields::Show::Minimized, Size{});
    check(!blank_picture(iconic, role(Kind::Toplevel), Size{}, false).has_value(),
          "a window with no size has nothing to be blank at");
}

}

int main() {
    minimized_before_any_picture();
    pictured_or_not_minimized();
    only_toplevels();
    no_size();
    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
