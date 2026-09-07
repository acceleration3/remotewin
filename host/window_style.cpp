#include "host/window_style.hpp"

#include <utility>

namespace rwin::host {

namespace {

// The style bits, named here because the host does not include windows.h.
constexpr std::uint32_t kCaption = 0x00C00000;
constexpr std::uint32_t kThickFrame = 0x00040000;
constexpr std::uint32_t kMinimizeBox = 0x00020000;
constexpr std::uint32_t kMaximizeBox = 0x00010000;
constexpr std::uint32_t kPopup = 0x80000000;
constexpr std::uint32_t kExToolWindow = 0x00000080;
constexpr std::uint32_t kExTopMost = 0x00000008;

// Windows' own registered classes for the two window kinds that are not
// just "a window": the menu class and the dialog class.
constexpr const char* kMenuClass = "#32768";
constexpr const char* kDialogClass = "#32770";

}  // namespace

WindowStyle::WindowStyle(std::string class_name, std::uint32_t style,
                         std::uint32_t extended_style)
    : class_name_(std::move(class_name)),
      style_(style),
      extended_style_(extended_style),
      is_menu_(class_name_ == kMenuClass),
      is_dialog_(class_name_ == kDialogClass) {}

bool WindowStyle::has(std::uint32_t bit) const noexcept { return (style_ & bit) == bit; }

bool WindowStyle::is_popup() const noexcept {
    // WS_POPUP alone does not make a popup: plenty of ordinary
    // application windows set it and then add a caption. What makes a
    // window a popup is having no caption to be dragged by.
    return is_menu_ || ((style_ & kPopup) != 0 && !has(kCaption));
}

bool WindowStyle::is_resizable() const noexcept {
    return has(kThickFrame) && !is_menu_ && !is_popup();
}

Qt::WindowFlags WindowStyle::window_flags() const {
    if (is_menu_) {
        // Qt::Popup takes the pointer grab and dismisses on a click
        // outside, which is what makes a menu feel like one.
        return Qt::Popup | Qt::FramelessWindowHint;
    }
    if (is_popup()) {
        return Qt::Tool | Qt::FramelessWindowHint;
    }

    Qt::WindowFlags flags = is_dialog_ ? Qt::Dialog : Qt::Window;
    if (has(kCaption)) {
        flags |= Qt::WindowTitleHint | Qt::WindowCloseButtonHint;
    } else {
        flags |= Qt::FramelessWindowHint;
    }
    if (has(kMinimizeBox)) flags |= Qt::WindowMinimizeButtonHint;
    if (has(kMaximizeBox)) flags |= Qt::WindowMaximizeButtonHint;
    if ((extended_style_ & kExToolWindow) != 0) flags |= Qt::Tool;
    if ((extended_style_ & kExTopMost) != 0) flags |= Qt::WindowStaysOnTopHint;
    return flags;
}

}  // namespace rwin::host
