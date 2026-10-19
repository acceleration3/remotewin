#pragma once

#include <Qt>

#include <cstdint>

namespace rwin::host {

class GuestWindow;

// A guest window's Win32 words, read as the behaviours Windows documents
// for each.
//
// Nothing here decides what a window is. Each question is one word — a
// style bit, a class bit, a DWM attribute, the z-band — and the one thing
// Windows says that word does; the mirror then does that same thing with
// what Qt and the compositor offer. A window the guest gave WS_THICKFRAME
// can be resized, one given WS_EX_TOPMOST stays on top, one given
// WS_DISABLED takes no input. Two answers are Windows' own, worked out in
// the guest and carried as they came: whether the taskbar gives the window
// a button, and whether Windows shades it.
class Win32Style {
public:
    // GWL_STYLE.
    static constexpr std::uint32_t kWsDisabled = 0x08000000;
    static constexpr std::uint32_t kWsBorder = 0x00800000;
    static constexpr std::uint32_t kWsDlgFrame = 0x00400000;
    static constexpr std::uint32_t kWsCaption = kWsBorder | kWsDlgFrame;
    static constexpr std::uint32_t kWsPopup = 0x80000000;
    static constexpr std::uint32_t kWsSysMenu = 0x00080000;
    static constexpr std::uint32_t kWsThickFrame = 0x00040000;
    static constexpr std::uint32_t kWsMinimizeBox = 0x00020000;
    static constexpr std::uint32_t kWsMaximizeBox = 0x00010000;
    // GWL_EXSTYLE.
    static constexpr std::uint32_t kWsExTopMost = 0x00000008;
    static constexpr std::uint32_t kWsExTransparent = 0x00000020;
    static constexpr std::uint32_t kWsExToolWindow = 0x00000080;
    static constexpr std::uint32_t kWsExContextHelp = 0x00000400;
    static constexpr std::uint32_t kWsExAppWindow = 0x00040000;
    static constexpr std::uint32_t kWsExLayered = 0x00080000;
    static constexpr std::uint32_t kWsExNoActivate = 0x08000000;
    // GCL_STYLE.
    static constexpr std::uint32_t kCsNoClose = 0x00000200;
    static constexpr std::uint32_t kCsDropShadow = 0x00020000;
    // SetLayeredWindowAttributes flags.
    static constexpr std::uint32_t kLwaColorKey = 0x00000001;
    static constexpr std::uint32_t kLwaAlpha = 0x00000002;
    // The z-order band every application window is in. Windows stacks
    // the higher bands — the shell's, the lock screen's, UIAccess tools'
    // — above the whole of this one, topmost or not.
    static constexpr std::uint32_t kBandDesktop = 1;

    struct Words {
        std::uint32_t style = 0;
        std::uint32_t extended_style = 0;
        std::uint32_t class_style = 0;
        std::uint32_t layered_flags = 0;
        std::uint8_t layered_alpha = 255;
        std::uint32_t color_key = 0;
        std::uint32_t band = kBandDesktop;
        // Owned by a window, rather than by none or by the hidden owner.
        bool owned = false;
        // The TASKBAR field's button bit: the taskbar's rule as Windows
        // applies it, immersive windows and ShowInSwitchers included.
        bool taskbar_button = false;
        // The FRAME field's shadow bit: CS_DROPSHADOW or a frame DWM draws,
        // and the desktop's "Show shadows under windows" on.
        bool shaded = false;

        [[nodiscard]] constexpr bool operator==(const Words&) const noexcept = default;
    };

    constexpr explicit Win32Style(Words words) noexcept : words_(words) {}
    explicit Win32Style(const GuestWindow& window) noexcept;

    [[nodiscard]] constexpr bool operator==(const Win32Style&) const noexcept = default;

    // WS_CAPTION: "The window has a title bar."
    [[nodiscard]] constexpr bool has_caption() const noexcept { return has(kWsCaption); }

    // WS_SYSMENU: "The window has a window menu on its title bar."
    [[nodiscard]] constexpr bool has_system_menu() const noexcept {
        return has(kWsSysMenu);
    }

    [[nodiscard]] constexpr bool has_minimize_box() const noexcept {
        return has(kWsMinimizeBox);
    }

    [[nodiscard]] constexpr bool has_maximize_box() const noexcept {
        return has(kWsMaximizeBox);
    }

    // WS_THICKFRAME: "The window has a sizing border."
    [[nodiscard]] constexpr bool sizable() const noexcept { return has(kWsThickFrame); }

    // WS_DISABLED: "A disabled window cannot receive input from the user."
    [[nodiscard]] constexpr bool disabled() const noexcept { return has(kWsDisabled); }

    // CS_NOCLOSE: "Disables Close on the window menu." Alt+F4 and the
    // close button do nothing to such a window.
    [[nodiscard]] constexpr bool closable() const noexcept { return !has_class(kCsNoClose); }

    // WS_EX_TOPMOST: "placed above all non-topmost windows and should stay
    // above them, even when the window is deactivated." A window in a
    // band above the desktop's is above all of those too.
    [[nodiscard]] constexpr bool topmost() const noexcept {
        return has_ex(kWsExTopMost) || words_.band > kBandDesktop;
    }

    // WS_EX_TOOLWINDOW: "does not appear in the taskbar or in the dialog
    // that appears when the user presses ALT+TAB."
    [[nodiscard]] constexpr bool tool_window() const noexcept {
        return has_ex(kWsExToolWindow);
    }

    // WS_EX_NOACTIVATE: "does not become the foreground window when the
    // user clicks it."
    [[nodiscard]] constexpr bool activates() const noexcept {
        return !has_ex(kWsExNoActivate);
    }

    // WS_EX_TRANSPARENT on a WS_EX_LAYERED window: "the shape of the
    // layered window will be ignored and the mouse events will be passed
    // to other windows underneath" (SetLayeredWindowAttributes).
    [[nodiscard]] constexpr bool click_through() const noexcept {
        return has_ex(kWsExLayered) && has_ex(kWsExTransparent);
    }

    // Whether Windows shades the window.
    [[nodiscard]] constexpr bool casts_shadow() const noexcept { return words_.shaded; }

    // SetLayeredWindowAttributes with LWA_ALPHA: the constant alpha the
    // whole window is blended with. 255 when none was set.
    [[nodiscard]] constexpr std::uint8_t opacity() const noexcept {
        return (words_.layered_flags & kLwaAlpha) != 0 ? words_.layered_alpha : 255;
    }

    // SetLayeredWindowAttributes with LWA_COLORKEY: "All pixels painted by
    // the window in this color will be transparent."
    [[nodiscard]] constexpr bool color_keyed() const noexcept {
        return (words_.layered_flags & kLwaColorKey) != 0;
    }

    // The keyed colour as Windows keeps it, a COLORREF: 0x00BBGGRR.
    [[nodiscard]] constexpr std::uint32_t color_key() const noexcept {
        return words_.color_key;
    }

    // Whether it has an owner. Windows keeps an owned window above its
    // owner, hides it with its owner, and destroys it with its owner.
    [[nodiscard]] constexpr bool owned() const noexcept { return words_.owned; }

    // Owned, and mirrored as a window of its own attached to its owner's
    // rather than as a popup placed against it.
    //
    // A window that activates and is a window rather than a popup is one
    // the user moves, types into and switches to: a dialog, a message box,
    // a palette. Windows draws a caption on every overlapped window, so one
    // without WS_POPUP had a caption until its program took it away to
    // draw its own - WPF's WindowStyle=None, which is how Affinity's
    // floating tool panels are made. Wayland gives a popup no keyboard
    // focus and no move, so such a window is a toplevel attached to its
    // owner's (xdg_toplevel.set_parent), which the compositor keeps above
    // its owner and groups with it. A WS_POPUP without a caption - a menu,
    // a dropdown, a tooltip - stays a popup, placed exactly where its owner
    // put it, and so does anything that never activates.
    [[nodiscard]] constexpr bool stands_alone() const noexcept {
        if (!words_.owned || !activates()) return false;
        return has_caption() || !has(kWsPopup);
    }

    // Whether the taskbar gives it a button of its own.
    [[nodiscard]] constexpr bool in_taskbar() const noexcept { return words_.taskbar_button; }

    // The Qt hints those behaviours are, for whichever surface kind the
    // mirror is. Frameless always: the guest's own chrome is in the
    // pixels, and a second title bar would be absurd.
    [[nodiscard]] Qt::WindowFlags hints() const noexcept;

    // The kind of toplevel: one with a taskbar entry, or one without.
    [[nodiscard]] Qt::WindowType toplevel_type() const noexcept;

private:
    [[nodiscard]] constexpr bool has(std::uint32_t bit) const noexcept {
        return (words_.style & bit) == bit;
    }
    [[nodiscard]] constexpr bool has_ex(std::uint32_t bit) const noexcept {
        return (words_.extended_style & bit) == bit;
    }
    [[nodiscard]] constexpr bool has_class(std::uint32_t bit) const noexcept {
        return (words_.class_style & bit) == bit;
    }

    Words words_;
};

}  // namespace rwin::host
