#pragma once

#include <Qt>

#include <cstdint>
#include <string>

namespace rwin::host {

// What a guest window is, decided from the things Windows decides it by:
// its class and its style bits.
//
// The host does not paint a copy of the guest's chrome. It asks its own
// compositor for a window of the same *kind* — a resizable frame, a
// dialog, a menu — and paints only the client pixels inside it. A menu
// then behaves like a menu: the compositor places it, dismisses it and
// keeps it above its owner, none of which a picture of a menu can do.
class WindowStyle {
public:
    WindowStyle(std::string class_name, std::uint32_t style,
                std::uint32_t extended_style);

    [[nodiscard]] bool is_menu() const noexcept { return is_menu_; }
    [[nodiscard]] bool is_dialog() const noexcept { return is_dialog_; }
    [[nodiscard]] bool is_popup() const noexcept;
    [[nodiscard]] bool is_resizable() const noexcept;

    [[nodiscard]] Qt::WindowFlags window_flags() const;

private:
    [[nodiscard]] bool has(std::uint32_t bit) const noexcept;

    std::string class_name_;
    std::uint32_t style_;
    std::uint32_t extended_style_;
    bool is_menu_;
    bool is_dialog_;
};

}  // namespace rwin::host
