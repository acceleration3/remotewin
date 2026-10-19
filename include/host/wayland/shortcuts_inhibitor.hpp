#pragma once

struct wl_seat;
struct wl_surface;
struct zwp_keyboard_shortcuts_inhibit_manager_v1;
struct zwp_keyboard_shortcuts_inhibitor_v1;

namespace rwin::host {

// The compositor's own shortcuts held back from one surface, for as long as
// this lives (zwp_keyboard_shortcuts_inhibit_manager_v1): Alt+Tab, Super
// and the rest reach the guest while a mirror is fullscreen or holds the
// pointer locked, as they reach a game on Windows. The compositor may ask
// the user first, and lets its own escape through.
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

}  // namespace rwin::host
