#pragma once

struct wl_seat;
struct wl_surface;
struct zwp_keyboard_shortcuts_inhibit_manager_v1;
struct zwp_keyboard_shortcuts_inhibitor_v1;

namespace rwin::host {
class ShortcutsInhibitor {
public:
    ShortcutsInhibitor(zwp_keyboard_shortcuts_inhibit_manager_v1* manager, wl_surface* surface, wl_seat* seat);
    ~ShortcutsInhibitor();

    ShortcutsInhibitor(const ShortcutsInhibitor&) = delete;
    ShortcutsInhibitor& operator=(const ShortcutsInhibitor&) = delete;
    ShortcutsInhibitor(ShortcutsInhibitor&&) = delete;
    ShortcutsInhibitor& operator=(ShortcutsInhibitor&&) = delete;

private:
    zwp_keyboard_shortcuts_inhibitor_v1* inhibitor_;
};

}
