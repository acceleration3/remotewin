#include "host/chrome/win32_style.hpp"

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
    check(style.in_taskbar(), "unowned, not a tool window: on the taskbar");
    check(style.toplevel_type() == Qt::Window, "a taskbar window is Qt::Window");
    check(style.opacity() == 255, "no LWA_ALPHA: opaque");
    check(!style.color_keyed(), "no LWA_COLORKEY: not keyed");
    check((style.hints() & Qt::FramelessWindowHint) != 0, "always frameless");
    check((style.hints() & Qt::NoDropShadowWindowHint) != 0, "unshaded: no shadow");
}

void window_style_bits() {
    check(Win32Style{Words{.style = Win32Style::kWsThickFrame}}.sizable(),
          "WS_THICKFRAME: sizable");
    check(Win32Style{Words{.style = Win32Style::kWsCaption}}.has_caption(),
          "WS_CAPTION: title bar");
    check(!Win32Style{Words{.style = Win32Style::kWsBorder}}.has_caption(),
          "WS_BORDER alone is not WS_CAPTION");
    check(Win32Style{Words{.style = Win32Style::kWsBorder}}.has_frame(),
          "WS_BORDER alone is a frame");
    check(Win32Style{Words{.style = Win32Style::kWsDisabled}}.disabled(),
          "WS_DISABLED: no input");
    const Win32Style boxes{Words{.style = Win32Style::kWsSysMenu | Win32Style::kWsMinimizeBox
                                          | Win32Style::kWsMaximizeBox}};
    check((boxes.hints() & Qt::WindowCloseButtonHint) != 0, "WS_SYSMENU: close button");
    check((boxes.hints() & Qt::WindowMinimizeButtonHint) != 0, "WS_MINIMIZEBOX");
    check((boxes.hints() & Qt::WindowMaximizeButtonHint) != 0, "WS_MAXIMIZEBOX");
}

void extended_style_bits() {
    const Win32Style pinned{Words{.extended_style = Win32Style::kWsExTopMost}};
    check(pinned.topmost(), "WS_EX_TOPMOST: topmost");
    check((pinned.hints() & Qt::WindowStaysOnTopHint) != 0, "topmost stays on top");

    const Win32Style quiet{Words{.extended_style = Win32Style::kWsExNoActivate}};
    check(!quiet.activates(), "WS_EX_NOACTIVATE: does not activate");
    check((quiet.hints() & Qt::WindowDoesNotAcceptFocus) != 0, "and takes no focus");
    check(!quiet.in_taskbar(), "WS_EX_NOACTIVATE: off the taskbar by default");

    const Win32Style tool{Words{.extended_style = Win32Style::kWsExToolWindow}};
    check(!tool.in_taskbar(), "WS_EX_TOOLWINDOW: off the taskbar");
    check(tool.toplevel_type() == Qt::Tool, "off the taskbar is Qt::Tool");

    const Win32Style see_through{Words{.extended_style = Win32Style::kWsExLayered
                                                          | Win32Style::kWsExTransparent}};
    check(see_through.click_through(), "WS_EX_LAYERED | WS_EX_TRANSPARENT: click-through");
    check((see_through.hints() & Qt::WindowTransparentForInput) != 0,
          "click-through is transparent for input");
    check(!Win32Style{Words{.extended_style = Win32Style::kWsExTransparent}}.click_through(),
          "WS_EX_TRANSPARENT alone is a paint order, not a hole");
}

void the_taskbar_rule() {
    check(!Win32Style{Words{.owned = true}}.in_taskbar(), "owned: no button of its own");
    check(Win32Style{Words{.extended_style = Win32Style::kWsExAppWindow, .owned = true}}
              .in_taskbar(),
          "WS_EX_APPWINDOW forces a button on an owned window");
    check(Win32Style{Words{.extended_style = Win32Style::kWsExAppWindow
                                             | Win32Style::kWsExToolWindow}}
              .in_taskbar(),
          "WS_EX_APPWINDOW forces a button on a tool window");
}

void class_style_bits() {
    const Win32Style shaded{Words{.class_style = Win32Style::kCsDropShadow}};
    check(shaded.casts_shadow(), "CS_DROPSHADOW: shaded");
    check((shaded.hints() & Qt::NoDropShadowWindowHint) == 0, "shaded keeps its shadow");

    const Win32Style stuck{Words{.style = Win32Style::kWsSysMenu,
                                 .class_style = Win32Style::kCsNoClose}};
    check(!stuck.closable(), "CS_NOCLOSE: not closable");
    check((stuck.hints() & Qt::WindowCloseButtonHint) == 0, "and no close button");
}

void dwm_frame() {
    check(Win32Style{Words{.style = Win32Style::kWsCaption, .dwm_frame = true}}.casts_shadow(),
          "a frame DWM draws has DWM's shadow");
    check(!Win32Style{Words{.dwm_frame = true}}.casts_shadow(),
          "DWM enabled on a frameless window draws nothing to shade");
    check(!Win32Style{Words{.style = Win32Style::kWsCaption}}.casts_shadow(),
          "a frame DWM is not drawing has no shadow");
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

void equality_is_over_every_word() {
    check(Win32Style{Words{}} == Win32Style{Words{}}, "equal words, equal style");
    check(!(Win32Style{Words{}} == Win32Style{Words{.dwm_frame = true}}),
          "one word apart, not equal");
}

}  // namespace

int main() {
    plain_words_are_a_plain_toplevel();
    window_style_bits();
    extended_style_bits();
    the_taskbar_rule();
    class_style_bits();
    dwm_frame();
    layering();
    bands();
    equality_is_over_every_word();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("win32_style: all checks passed\n");
    return EXIT_SUCCESS;
}
