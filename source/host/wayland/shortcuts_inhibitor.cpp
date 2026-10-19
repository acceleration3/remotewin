#include "host/wayland/shortcuts_inhibitor.hpp"

#include <keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h>
#include <wayland-client.h>

namespace rwin::host {

ShortcutsInhibitor::ShortcutsInhibitor(zwp_keyboard_shortcuts_inhibit_manager_v1* manager, wl_surface* surface,
                                       wl_seat* seat)
    : inhibitor_(zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(manager, surface, seat)) {}

ShortcutsInhibitor::~ShortcutsInhibitor() { zwp_keyboard_shortcuts_inhibitor_v1_destroy(inhibitor_); }

}  // namespace rwin::host
