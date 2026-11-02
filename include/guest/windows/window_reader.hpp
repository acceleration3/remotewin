#pragma once

#include "guest/windows/popup_intent.hpp"
#include "proto/window_fields.hpp"

#include <optional>
#include <string>

namespace rwin::guest {

class AppIdResolver;
class DesktopSettings;
class ProcessTree;
class ShellViews;
class WindowDirectory;

class WindowReader {
public:
    struct Reading {
        WindowFields fields;
        void* owner = nullptr;
        bool hidden_owner = false;
        void* anchor = nullptr;
    };

    WindowReader(const WindowDirectory& directory, const AppIdResolver& app_ids, const ShellViews& shell,
                 const DesktopSettings& settings, ProcessTree& processes) noexcept
        : directory_(directory), app_ids_(app_ids), shell_(shell), settings_(settings), processes_(processes) {}

    [[nodiscard]] static bool effectively_visible(void* window);

    [[nodiscard]] static bool top_level(void* window);

    [[nodiscard]] bool admissible(void* window) const;

    [[nodiscard]] Reading read(void* window, WindowId id, std::optional<PopupIntent::Point> opened_at) const;
    [[nodiscard]] WindowFields::AppId read_app_id(void* window) const;

private:
    [[nodiscard]] WindowFields::Show show_of(void* window, bool cloaked) const;
    [[nodiscard]] WindowFields::Geometry geometry_of(void* window, WindowId id) const;
    [[nodiscard]] std::uint8_t taskbar_of(void* window, std::uint32_t extended, bool& refused) const;
    [[nodiscard]] std::uint8_t frame_of(void* window, std::uint32_t style, std::uint32_t class_style) const;
    void read_owner(void* window, Reading& into) const;

    const WindowDirectory& directory_;
    const AppIdResolver& app_ids_;
    const ShellViews& shell_;
    const DesktopSettings& settings_;
    ProcessTree& processes_;
};

}
