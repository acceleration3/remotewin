#include "host/link/guest_windows.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::WindowFields;
using rwin::WindowId;
using rwin::host::GuestWindows;
using rwin::host::MirrorScope;

constexpr std::uint32_t kApp = 4242;
constexpr std::uint32_t kOther = 77;

WindowFields made_by(std::uint32_t process, std::string aumid = {}, std::string image = {}) {
    WindowFields fields;
    fields.show = WindowFields::Show::Shown;
    fields.app_id = {std::move(aumid), process, std::move(image)};
    return fields;
}

void nothing_to_go_by_is_everything() {
    GuestWindows windows;
    static_cast<void>(windows.tell(WindowId{0x10, 1}, made_by(kOther)));
    const MirrorScope scope{MirrorScope::Launch{}};
    check(scope.everything() && scope.belongs(WindowId{0x10, 1}, windows), "no scope: every window belongs");
}

void the_launched_process_and_what_it_owns() {
    GuestWindows windows;
    const WindowId main{0x10, 1};
    const WindowId dialog{0x20, 2};
    const WindowId menu{0x30, 3};
    const WindowId stranger{0x40, 4};
    static_cast<void>(windows.tell(main, made_by(kApp)));
    WindowFields helper = made_by(kOther);
    helper.owner = main;
    static_cast<void>(windows.tell(dialog, helper));
    WindowFields dropped = made_by(kOther);
    dropped.popup.anchor = dialog;
    static_cast<void>(windows.tell(menu, dropped));
    static_cast<void>(windows.tell(stranger, made_by(kOther)));
    const MirrorScope scope{MirrorScope::Launch{kApp, {}, {}}};
    check(scope.belongs(main, windows), "the launched process's window belongs");
    check(scope.belongs(dialog, windows), "a helper's dialog owned by it belongs");
    check(scope.belongs(menu, windows), "a menu placed against that dialog belongs");
    check(!scope.belongs(stranger, windows), "another program's window does not");
}

void a_hidden_owner_is_no_owner() {
    GuestWindows windows;
    WindowFields orphan = made_by(kOther);
    orphan.owner = WindowId::hidden_owner();
    static_cast<void>(windows.tell(WindowId{0x10, 1}, orphan));
    const MirrorScope scope{MirrorScope::Launch{kApp, {}, {}}};
    check(!scope.belongs(WindowId{0x10, 1}, windows), "the hidden owner is nobody's window");
}

void the_image_matches_as_windows_compares_paths() {
    GuestWindows windows;
    static_cast<void>(windows.tell(WindowId{0x10, 1}, made_by(kOther, {}, "C:\\Program Files\\App\\app.exe")));
    const MirrorScope scope{MirrorScope::Launch{std::nullopt, "c:/program files/app/APP.EXE", {}}};
    check(scope.belongs(WindowId{0x10, 1}, windows), "case and slashes do not matter");
}

void an_application_id_is_claimed_and_followed() {
    GuestWindows windows;
    static_cast<void>(windows.tell(WindowId{0x10, 1}, made_by(kApp, "Vendor.App")));
    static_cast<void>(windows.tell(WindowId{0x20, 2}, made_by(kOther, "Vendor.App")));
    static_cast<void>(windows.tell(WindowId{0x30, 3}, made_by(kOther, "Other.App")));
    MirrorScope scope{MirrorScope::Launch{kApp, {}, {}}};
    check(!scope.belongs(WindowId{0x20, 2}, windows), "before it is claimed, another process's window is not");
    const std::vector<std::string> claimed = scope.claim(windows);
    check(claimed == std::vector<std::string>{"Vendor.App"}, "the launched process's id is claimed once");
    check(scope.claim(windows).empty(), "and not again");
    check(scope.belongs(WindowId{0x20, 2}, windows), "another process's window of that id then belongs");
    check(!scope.belongs(WindowId{0x30, 3}, windows), "one of another id does not");
    MirrorScope exact{MirrorScope::Launch{kApp, {}, {}, false}};
    check(exact.claim(windows).empty() && !exact.belongs(WindowId{0x20, 2}, windows),
          "kept to the process alone, another process of the same id is not the application's");
    const MirrorScope store{MirrorScope::Launch{std::nullopt, {}, {"Other.App"}}};
    check(store.belongs(WindowId{0x30, 3}, windows), "an id given at launch belongs from the start");
}

void a_loop_ends() {
    GuestWindows windows;
    WindowFields a = made_by(kOther);
    a.owner = WindowId{0x20, 2};
    WindowFields b = made_by(kOther);
    b.owner = WindowId{0x10, 1};
    static_cast<void>(windows.tell(WindowId{0x10, 1}, a));
    static_cast<void>(windows.tell(WindowId{0x20, 2}, b));
    const MirrorScope scope{MirrorScope::Launch{kApp, {}, {}}};
    check(!scope.belongs(WindowId{0x10, 1}, windows), "owners in a loop belong to nobody, and the walk ends");
}

void a_resync_forgets_what_it_does_not_say() {
    GuestWindows windows;
    static_cast<void>(windows.tell(WindowId{0x10, 1}, made_by(kApp)));
    static_cast<void>(windows.tell(WindowId{0x20, 2}, made_by(kApp)));
    windows.begin_sync();
    const GuestWindows::Told told = windows.tell(WindowId{0x10, 1}, made_by(kOther));
    check(told.known && told.changed == WindowFields::kAppId, "said again: known, with what changed");
    const std::vector<WindowId> gone = windows.end_sync();
    check(gone.size() == 1 && gone.front() == WindowId{0x20, 2}, "the window not said again is gone");
    check(windows.find(WindowId{0x20, 2}) == nullptr && windows.size() == 1, "and forgotten");
    check(!windows.change(WindowId{0x99, 9}, made_by(kApp)), "a change to a window not known changes nothing");
}

void the_stacking_comes_first() {
    GuestWindows windows;
    static_cast<void>(windows.tell(WindowId{0x10, 1}, made_by(kApp)));
    static_cast<void>(windows.tell(WindowId{0x20, 2}, made_by(kApp)));
    windows.restack(rwin::ZOrder{WindowId{0x20, 2}, {WindowId{0x20, 2}, rwin::kMonitorWindow}});
    const auto stacked = windows.stacked();
    check(stacked.size() == 2 && stacked[0]->id() == WindowId{0x20, 2} && stacked[1]->id() == WindowId{0x10, 1},
          "stacked windows first, the marker skipped, the rest after");
    check(windows.find_hwnd(0x10) != nullptr && windows.find_hwnd(0x10)->id() == WindowId{0x10, 1},
          "found by its handle alone");
}

}  // namespace

int main() {
    nothing_to_go_by_is_everything();
    the_launched_process_and_what_it_owns();
    a_hidden_owner_is_no_owner();
    the_image_matches_as_windows_compares_paths();
    an_application_id_is_claimed_and_followed();
    a_loop_ends();
    a_resync_forgets_what_it_does_not_say();
    the_stacking_comes_first();
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("mirror_scope: all checks passed\n");
    return EXIT_SUCCESS;
}
