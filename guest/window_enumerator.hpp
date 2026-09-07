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
    std::uint32_t process_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t style = 0;
    std::uint32_t extended_style = 0;
    std::string title;
    std::string class_name;
};

// Every top-level window worth showing: visible, sized, not a tool window
// and not one of the shell's own.
class WindowEnumerator {
public:
    [[nodiscard]] std::vector<GuestWindow> visible_windows() const;

    // The same, narrowed to one process. Empty means every process.
    [[nodiscard]] std::vector<GuestWindow> windows_of(std::uint32_t process_id) const;
};

}  // namespace rwin::guest
