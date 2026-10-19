#include "host/chrome/win32_style.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::host::Win32Style;
using Words = Win32Style::Words;

// Each check below is one word and the one behaviour Windows documents
// for it. No window is named; no two words are read together except
// where Windows itself reads them together.

void plain_words_are_a_plain_toplevel() {
    const Win32Style style{Words{}};
    check(!style.sizable(), "no WS_THICKFRAME: not sizable");
    check(!style.has_caption(), "no WS_CAPTION: no title bar");
    check(style.activates(), "no WS_EX_NOACTIVATE: activates");
    check(style.closable(), "no CS_NOCLOSE: closable");
    check(!style.topmost(), "desktop band, no WS_EX_TOPMOST: not topmost");
    check(!style.in_taskbar(), "no taskbar button said: off the taskbar");
    check(style.opacity() == 255, "no LWA_ALPHA: opaque");
    check(!style.color_keyed(), "no LWA_COLORKEY: not keyed");
}

void window_style_bits() {
    check(Win32Style{Words{.style = Win32Style::kWsThickFrame}}.sizable(),
          "WS_THICKFRAME: sizable");
    check(Win32Style{Words{.style = Win32Style::kWsCaption}}.has_caption(),
          "WS_CAPTION: title bar");
    check(!Win32Style{Words{.style = Win32Style::kWsBorder}}.has_caption(),
          "WS_BORDER alone is not WS_CAPTION");
    check(Win32Style{Words{.style = Win32Style::kWsDisabled}}.disabled(),
          "WS_DISABLED: no input");
    const Win32Style boxes{Words{.style = Win32Style::kWsSysMenu | Win32Style::kWsMinimizeBox
                                          | Win32Style::kWsMaximizeBox}};
    check(boxes.has_system_menu() && boxes.has_minimize_box() && boxes.has_maximize_box(),
          "WS_SYSMENU, WS_MINIMIZEBOX, WS_MAXIMIZEBOX");
}

void extended_style_bits() {
    const Win32Style pinned{Words{.extended_style = Win32Style::kWsExTopMost}};
    check(pinned.topmost(), "WS_EX_TOPMOST: topmost");

    const Win32Style quiet{Words{.extended_style = Win32Style::kWsExNoActivate}};
    check(!quiet.activates(), "WS_EX_NOACTIVATE: does not activate");
    check(Win32Style{Words{.extended_style = Win32Style::kWsExToolWindow}}.tool_window(),
          "WS_EX_TOOLWINDOW: a tool window");

    const Win32Style see_through{Words{.extended_style = Win32Style::kWsExLayered
                                                          | Win32Style::kWsExTransparent}};
    check(see_through.click_through(), "WS_EX_LAYERED | WS_EX_TRANSPARENT: click-through");
    check(!Win32Style{Words{.extended_style = Win32Style::kWsExTransparent}}.click_through(),
          "WS_EX_TRANSPARENT alone is a paint order, not a hole");
}

// Windows' own rule, applied in the guest: the words are not read again.
void the_taskbar_is_the_guests_to_say() {
    const Win32Style button{Words{.owned = true, .taskbar_button = true}};
    check(button.in_taskbar(), "a button said: on the taskbar, owned or not");
    check(!Win32Style{Words{.extended_style = Win32Style::kWsExAppWindow}}.in_taskbar(),
          "WS_EX_APPWINDOW without the guest's button is no button");
}

void class_style_bits() {

    const Win32Style stuck{Words{.style = Win32Style::kWsSysMenu,
                                 .class_style = Win32Style::kCsNoClose}};
    check(!stuck.closable(), "CS_NOCLOSE: not closable");
}

// Windows' answer, from the guest: CS_DROPSHADOW, DWM's frame and the
// desktop's "Show shadows under windows" are its to weigh.
void shading_is_the_guests_to_say() {
    const Win32Style shaded{Words{.shaded = true}};
    check(shaded.casts_shadow(), "shaded: casts a shadow");
    check(!Win32Style{Words{.class_style = Win32Style::kCsDropShadow}}.casts_shadow(),
          "CS_DROPSHADOW the guest did not shade is unshaded");
}

void layering() {
    const Win32Style faded{Words{.extended_style = Win32Style::kWsExLayered,
                                 .layered_flags = Win32Style::kLwaAlpha,
                                 .layered_alpha = 128}};
    check(faded.opacity() == 128, "LWA_ALPHA: the constant alpha");
    check(Win32Style{Words{.layered_alpha = 128}}.opacity() == 255,
          "an alpha without LWA_ALPHA is not used");

    const Win32Style keyed{Words{.layered_flags = Win32Style::kLwaColorKey,
                                 .color_key = 0x00FF00FF}};
    check(keyed.color_keyed(), "LWA_COLORKEY: keyed");
    check(keyed.color_key() == 0x00FF00FF, "the key as a COLORREF");
}

void bands() {
    check(!Win32Style{Words{.band = Win32Style::kBandDesktop}}.topmost(),
          "the desktop band is where windows stack normally");
    check(Win32Style{Words{.band = Win32Style::kBandDesktop + 1}}.topmost(),
          "a band above the desktop's is above all of it");
}

void an_owned_dialog_stands_alone() {
    check(Win32Style{Words{.style = Win32Style::kWsCaption, .owned = true}}.stands_alone(),
          "owned, with a caption: attached to its owner, not a popup of it");
    check(!Win32Style{Words{.style = Win32Style::kWsCaption}}.stands_alone(),
          "not owned: nothing to stand apart from");
    check(!Win32Style{Words{.style = Win32Style::kWsPopup | Win32Style::kWsBorder, .owned = true}}.stands_alone(),
          "owned, a WS_POPUP with no caption: a popup");
}

bool owned_stands_alone(std::uint32_t style, std::uint32_t extended) {
    return Win32Style{Words{.style = style, .extended_style = extended, .owned = true}}.stands_alone();
}

// What the guest lists for windows met in the field.
void owned_windows_as_listed() {
    constexpr std::uint32_t kPopup = Win32Style::kWsPopup;
    constexpr std::uint32_t kNoActivate = Win32Style::kWsExNoActivate;
    check(owned_stands_alone(0x00CF0000, 0x80), "a tool window with a caption is a window of its own");
    check(!owned_stands_alone(kPopup | Win32Style::kWsBorder, 0), "a bordered popup is a popup");
    check(!owned_stands_alone(kPopup, 0), "a menu is a popup");
    check(!owned_stands_alone(Win32Style::kWsCaption, kNoActivate),
          "one that never activates stays a popup: it is never typed into");
    check(owned_stands_alone(0x16080000, 0x00080000),
          "Affinity's floating panel, an overlapped window with its caption taken away, is a window");
    check(!owned_stands_alone(0x96000000, 0x08080088), "a WPF dropdown (WS_POPUP, no-activate, topmost) is a popup");
    check(!owned_stands_alone(0x9e000028, 0x00080000), "a disabled WS_POPUP overlay is a popup");
}

void equality_is_over_every_word() {
    check(Win32Style{Words{}} == Win32Style{Words{}}, "equal words, equal style");
    check(!(Win32Style{Words{}} == Win32Style{Words{.shaded = true}}),
          "one word apart, not equal");
}

}  // namespace

int main() {
    plain_words_are_a_plain_toplevel();
    window_style_bits();
    extended_style_bits();
    the_taskbar_is_the_guests_to_say();
    class_style_bits();
    shading_is_the_guests_to_say();
    layering();
    bands();
    an_owned_dialog_stands_alone();
    owned_windows_as_listed();
    equality_is_over_every_word();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("win32_style: all checks passed\n");
    return EXIT_SUCCESS;
}
