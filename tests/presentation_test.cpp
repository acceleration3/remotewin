#include "host/mirror/presentation.hpp"

#include "host/mirror/mirror_role.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

using rwin::WindowId;
using rwin::host::MirrorRole;
using rwin::host::Presentation;
using Kind = rwin::host::MirrorRole::Kind;

struct Watched {
    Presentation presentation;
    int began = 0;

    Watched() {
        presentation.began.connect([this] { ++began; });
    }
};

void nothing_presented() {
    const Watched watched;
    check(!watched.presentation.begun() && watched.began == 0, "an application with nothing on the host has not begun");
}

void first_picture() {
    Watched watched;
    watched.presentation.pictured();
    check(watched.presentation.begun() && watched.began == 1, "the first picture of a window begins it");
    watched.presentation.pictured();
    watched.presentation.mapped(MirrorRole{Kind::Toplevel, WindowId{}});
    check(watched.began == 1, "it begins once");
}

void toplevel_mapped_without_a_picture() {
    Watched watched;
    watched.presentation.mapped(MirrorRole{Kind::Toplevel, WindowId{}});
    check(watched.presentation.begun() && watched.began == 1,
          "a toplevel mapped with no picture, as a minimised window is, begins it");
    watched.presentation.pictured();
    check(watched.began == 1, "its picture later does not begin it again");
}

void popups_and_parts() {
    Watched watched;
    watched.presentation.mapped(MirrorRole{Kind::Popup, WindowId{0x10, 1}});
    watched.presentation.mapped(MirrorRole{Kind::Subsurface, WindowId{0x10, 1}});
    check(!watched.presentation.begun() && watched.began == 0, "a popup or a part mapped does not begin it");
}

}

int main() {
    nothing_presented();
    first_picture();
    toplevel_mapped_without_a_picture();
    popups_and_parts();
    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}
