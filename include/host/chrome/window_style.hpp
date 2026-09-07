#pragma once

#include <Qt>

#include <cstdint>
#include <string>

namespace rwin::host {

// What a guest window is, decided from the things Windows decides it by:
// its class and its style bits.
//
// The host never draws a title bar. The guest already has one, drawn the
// way that application draws it, and it arrives in the captured pixels —
// two would be absurd. So every mirror is frameless, and what this
// decides is the *kind* of window to ask the compositor for: a menu
// behaves like a menu, placed and dismissed by the compositor, which a
// picture of a menu cannot do; a dialog is a dialog; and whether the
// window may be resized at all.
//
// What the guest's own title bar promises is honoured by NonClientArea,
// which turns a press on those pixels into an action on this window.
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
