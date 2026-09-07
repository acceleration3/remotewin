#include "host/chrome/window_style.hpp"

#include <utility>

namespace rwin::host {

namespace {

// The style bits, named here because the host does not include windows.h.
constexpr std::uint32_t kCaption = 0x00C00000;
constexpr std::uint32_t kThickFrame = 0x00040000;
constexpr std::uint32_t kPopup = 0x80000000;
constexpr std::uint32_t kExToolWindow = 0x00000080;
constexpr std::uint32_t kExTopMost = 0x00000008;

// Windows' own registered classes for the two window kinds that are not
// just "a window": the menu class and the dialog class.
constexpr const char* kMenuClass = "#32768";
constexpr const char* kDialogClass = "#32770";
// What a WinUI application opens instead of a classic menu. Notepad's
// File menu is one of these.
constexpr const char* kXamlPopupClass = "Xaml_WindowedPopupClass";

}  // namespace

WindowStyle::WindowStyle(std::string class_name, std::uint32_t style,
                         std::uint32_t extended_style, bool owned)
    : class_name_(std::move(class_name)),
      style_(style),
      extended_style_(extended_style),
      owned_(owned),
      is_menu_(class_name_ == kMenuClass || class_name_ == kXamlPopupClass),
      is_dialog_(class_name_ == kDialogClass) {}

bool WindowStyle::has(std::uint32_t bit) const noexcept { return (style_ & bit) == bit; }

bool WindowStyle::is_popup() const noexcept {
    // Only a menu. Being owned and captionless does not make a window a
    // menu — an application's floating panels are exactly that and are
    // ordinary windows that happen to draw their own frame. Treating them
    // as popups made them surfaces the compositor positions from their
    // parent, which cannot be moved or resized at all, so Photoshop's
    // docks arrived unmovable.
    return is_menu_;
}

bool WindowStyle::is_resizable() const noexcept {
    return has(kThickFrame) && !is_menu_ && !is_popup();
}

Qt::WindowFlags WindowStyle::window_flags() const {
    // Frameless in every case; see the header. Only the kind varies.
    //
    // A popup is Qt::ToolTip because that is the only kind Qt turns into
    // an xdg_popup, which is the only Wayland surface a client may
    // position: it is placed by an offset from its parent. Qt::Tool is a
    // plain toplevel, and a toplevel cannot be positioned at all — the
    // compositor puts it in the middle of its parent, which is exactly
    // where every menu was landing. Qt::Popup would position correctly
    // too, but it takes a pointer grab and closes itself on a click
    // outside, and the guest is the one that decides when its menu is
    // finished.
    if (is_popup()) {
        // NoDropShadow because the guest's shadow is already in the
        // pixels: the compositor adding its own puts a second one around
        // the first, which is the frame that appears around a menu.
        return Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    }

    Qt::WindowFlags flags = Qt::FramelessWindowHint;
    if (is_dialog_) {
        flags |= Qt::Dialog;
    } else {
        flags |= Qt::Window;
    }
    // An owned window belongs to another and is not an application in
    // its own right: a panel, a palette, an inspector. Qt::Tool keeps it
    // above its owner and out of the task bar, while leaving it a real
    // toplevel the compositor will move and resize.
    if (owned_ || (extended_style_ & kExToolWindow) != 0) flags |= Qt::Tool;
    if ((extended_style_ & kExTopMost) != 0) flags |= Qt::WindowStaysOnTopHint;
    return flags;
}

}  // namespace rwin::host
