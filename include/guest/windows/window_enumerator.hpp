#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rwin::guest {

// One top-level window as the guest sees it.
//
// The class name and the styles are carried from the start because the
// host has two ways to show a window: paint the pixels, or rebuild it out
// of native widgets. The second needs to know a Button is a Button.
struct GuestWindow {
    std::uint64_t handle = 0;
    // The window this one belongs to, if any. A menu, a dropdown or a
    // dialog is placed against its owner, not against the desktop.
    std::uint64_t owner = 0;
    std::uint32_t process_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    std::string title;
    std::string class_name;
    bool minimized = false;
    bool maximized = false;
};

// Every top-level window worth showing.
//
// "Visible" is not enough. Windows keeps windows that are mapped and
// sized and report themselves visible, but which the compositor is not
// drawing — cloaked, in DWM's word. A console has one of these beside
// its real window, identical in class, size and style, and mirroring it
// puts up a second empty copy of the same terminal.
class LaunchScope;

class WindowEnumerator {
public:
    // Narrowed to the application ``scope`` describes.
    [[nodiscard]] std::vector<GuestWindow> windows_in(const LaunchScope& scope) const;
};

}  // namespace rwin::guest
