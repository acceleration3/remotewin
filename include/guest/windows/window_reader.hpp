#pragma once

#include "guest/windows/popup_intent.hpp"
#include "proto/window_fields.hpp"

#include <optional>
#include <string>

namespace rwin::guest {

class AppIdResolver;
class DesktopSettings;
class ShellViews;
class WindowDirectory;

// Reads one window as Windows' own RAIL server reads it
// (docs/rail-model.md §2.1-§2.4): every event about a window recomputes its
// whole record from what Windows keeps now, and what differs from the last
// record sent is the delta.
//
// Two fields name other windows, which only the tracker can give ids to:
// the reading carries their handles, and the tracker fills the ids in.
class WindowReader {
public:
    struct Reading {
        // Every field but APPID; OWNER and POPUP.anchor left none.
        WindowFields fields;
        // GW_OWNER as RAIL normalises it: a child owner taken to its root,
        // the desktop as none. Null with ``hidden_owner`` when the owner is
        // invisible, empty, or minimised to nothing.
        void* owner = nullptr;
        bool hidden_owner = false;
        // The window it opens against (PopupIntent), with POPUP's
        // rectangles already relative to that window's frame.
        void* anchor = nullptr;
    };

    WindowReader(const WindowDirectory& directory, const AppIdResolver& app_ids, const ShellViews& shell,
                 const DesktopSettings& settings) noexcept
        : directory_(directory), app_ids_(app_ids), shell_(shell), settings_(settings) {}

    // IsWindowVisible, or redraw-suspended: WM_SETREDRAW(FALSE) clears
    // WS_VISIBLE, and user32 marks such a window with the SysSetRedraw
    // property. RAIL counts it as shown, and so does this.
    [[nodiscard]] static bool effectively_visible(void* window);

    // Top-level as RAIL admits it: the desktop's child, or a WS_POPUP.
    [[nodiscard]] static bool top_level(void* window);

    // Admitted at all: a top-level window that is not one of the Explorer
    // shell's own - the desktop, the taskbars, and what the shell manages
    // and keeps out of the switchers - nor one of remotewin's.
    [[nodiscard]] bool admissible(void* window) const;

    // ``opened_at``: where the pointer was when the window was first seen
    // shown, which a context menu or a tooltip opened from.
    [[nodiscard]] Reading read(void* window, WindowId id, std::optional<PopupIntent::Point> opened_at) const;
    [[nodiscard]] WindowFields::AppId read_app_id(void* window) const;

private:
    [[nodiscard]] WindowFields::Show show_of(void* window, bool cloaked) const;
    [[nodiscard]] WindowFields::Geometry geometry_of(void* window, WindowId id) const;
    // The TASKBAR bits, and whether the shell refuses the window a button
    // whatever its styles say.
    [[nodiscard]] std::uint8_t taskbar_of(void* window, std::uint32_t extended, bool& refused) const;
    [[nodiscard]] std::uint8_t frame_of(void* window, std::uint32_t style, std::uint32_t class_style) const;
    void read_owner(void* window, Reading& into) const;

    const WindowDirectory& directory_;
    const AppIdResolver& app_ids_;
    const ShellViews& shell_;
    const DesktopSettings& settings_;
};

}  // namespace rwin::guest
