#include "conform/expectation.hpp"

#include <QString>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

using rwin::WindowTruth;
using rwin::conform::Expectation;
using rwin::host::MirrorState;

// An ordinary overlapped window as Windows reports it, and a mirror that
// does everything right by it.
WindowTruth overlapped() {
    WindowTruth truth;
    truth.handle = 0x500c4;
    truth.style = 0x14cf0000;
    truth.extended_style = 0x100;
    truth.title = "overlapped";
    truth.window = {400, 300, 778, 585};
    truth.visible = {408, 300, 770, 577};
    truth.client = {409, 336, 769, 576};
    truth.shown = true;
    truth.enabled = true;
    truth.dwm_frame = true;
    return truth;
}

MirrorState faithful() {
    MirrorState mirror;
    mirror.handle = 0x500c4;
    mirror.flags = Qt::Window | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    mirror.visible = true;
    mirror.forwards_input = true;
    mirror.resizable = true;
    mirror.title = QStringLiteral("overlapped");
    mirror.device_pixel_ratio = 1.2;
    mirror.picture = QRect(0, 0, 362, 277);
    return mirror;
}

const Expectation::Check* find(const std::vector<Expectation::Check>& checks, const char* property) {
    for (const Expectation::Check& check : checks) {
        if (check.property == QLatin1String(property)) return &check;
    }
    return nullptr;
}

bool passes(const std::vector<Expectation::Check>& checks, const char* property) {
    const Expectation::Check* found = find(checks, property);
    return found != nullptr && found->passed;
}

bool fails(const std::vector<Expectation::Check>& checks, const char* property) {
    const Expectation::Check* found = find(checks, property);
    return found != nullptr && !found->passed;
}

std::vector<Expectation::Check> checked(const WindowTruth& truth, const MirrorState& mirror,
                                        std::optional<bool> shadow = false,
                                        bool drop_shadows = true) {
    return Expectation{truth, truth, std::nullopt, shadow, drop_shadows}.against(mirror, nullptr);
}

void a_faithful_mirror_passes_every_row() {
    for (const Expectation::Check& row : checked(overlapped(), faithful())) {
        if (!row.passed) std::printf("  %s: %s vs %s\n", qPrintable(row.property),
                                     qPrintable(row.expected), qPrintable(row.actual));
        check(row.passed, "a mirror doing what Windows does passes");
    }
}

void topmost_wants_the_hint() {
    WindowTruth truth = overlapped();
    truth.extended_style |= 0x8;
    check(fails(checked(truth, faithful()), "stays_on_top"), "WS_EX_TOPMOST without the hint fails");
    MirrorState pinned = faithful();
    pinned.flags |= Qt::WindowStaysOnTopHint;
    check(passes(checked(truth, pinned), "stays_on_top"), "and with it passes");
    check(fails(checked(overlapped(), pinned), "stays_on_top"), "the hint without WS_EX_TOPMOST fails");
}

void the_taskbar_rule_is_windows_own() {
    MirrorState tool = faithful();
    tool.flags = Qt::Tool | Qt::FramelessWindowHint;
    WindowTruth palette = overlapped();
    palette.extended_style |= 0x80;
    check(passes(checked(palette, tool), "taskbar"), "WS_EX_TOOLWINDOW: no button");
    WindowTruth forced = palette;
    forced.extended_style |= 0x40000;
    check(fails(checked(forced, tool), "taskbar"), "WS_EX_APPWINDOW forces a button even on a tool window");
    WindowTruth quiet = overlapped();
    quiet.extended_style |= 0x08000000;
    check(fails(checked(quiet, faithful()), "taskbar"), "WS_EX_NOACTIVATE has no button by default");
    WindowTruth owned = overlapped();
    owned.owner = 0x1234;
    check(fails(checked(owned, faithful()), "taskbar"), "an owned window has no button");
    const Expectation::Check* plain = find(checked(owned, faithful()), "taskbar");
    check(plain != nullptr && plain->failed(), "and a mirror giving it one fails outright");
    WindowTruth owned_app = owned;
    owned_app.extended_style |= 0x40000;
    MirrorState popup = faithful();
    popup.popup_of = 0x1234;
    popup.flags = Qt::ToolTip | Qt::FramelessWindowHint;
    const auto rows = checked(owned_app, popup);
    const Expectation::Check* limited = find(rows, "taskbar");
    check(limited != nullptr && !limited->passed && !limited->failed() && !limited->known_limit.isEmpty(),
          "an owned WS_EX_APPWINDOW popup without a button is a known limit, not a failure");
}

void layering_is_read_from_the_attributes() {
    WindowTruth layered = overlapped();
    layered.extended_style |= 0x80000;
    layered.layered_flags = 3;
    layered.layered_alpha = 128;
    layered.color_key = 0x00FF00FF;
    MirrorState mirror = faithful();
    check(fails(checked(layered, mirror), "opacity"), "an alpha the mirror does not paint with fails");
    mirror.opacity = 128;
    mirror.color_keyed = true;
    mirror.color_key = 0x00FF00FF;
    const auto rows = checked(layered, mirror);
    check(passes(rows, "opacity") && passes(rows, "color_key"), "both attributes followed pass");
    const Expectation::Check* key = find(rows, "color_key");
    check(key != nullptr && key->expected == QStringLiteral("#ff00ff"), "a key is written as #rrggbb");

    WindowTruth unlayered = layered;
    unlayered.extended_style &= ~0x80000u;
    check(fails(checked(unlayered, mirror), "opacity"),
          "attributes of a window that is not WS_EX_LAYERED mean nothing");
}

void a_mirror_that_moves_its_window_is_caught() {
    WindowTruth made = overlapped();
    made.zoomed = true;
    made.window = {-9, -9, 2569, 1391};
    const WindowTruth later = overlapped();
    const auto rows = Expectation{made, later, std::nullopt, false, true}.against(faithful(), nullptr);
    const Expectation::Check* row = find(rows, "undisturbed");
    check(row != nullptr && !row->passed, "a maximised window found restored fails");
    check(row != nullptr && row->expected.startsWith(QStringLiteral("maximized")),
          "and says what it was made as");
}

void an_owned_popup_is_placed_against_its_owner() {
    const WindowTruth owner = overlapped();
    WindowTruth popup = overlapped();
    popup.handle = 0x600aa;
    popup.owner = owner.handle;
    popup.style = 0x80800000;
    popup.visible = {560, 420, 762, 542};
    MirrorState owner_mirror = faithful();
    owner_mirror.position = QPoint(100, 100);
    MirrorState popup_mirror = faithful();
    popup_mirror.popup_of = owner.handle;
    popup_mirror.flags = Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    popup_mirror.resizable = false;
    popup_mirror.picture = QRect(0, 0, 202, 122);
    // 152 guest pixels is 126.67 units at 1.2: placed at 127, which is
    // 152.4 pixels, inside the rounding.
    popup_mirror.placed = QPoint(100 + 127, 100 + 100);
    // The compositor's own answer, a unit along on its grid, is not the
    // mirror's decision.
    popup_mirror.position = QPoint(100 + 128, 100 + 100);
    const Expectation expectation{popup, popup, owner, false, true};
    const auto placed = expectation.against(popup_mirror, &owner_mirror);
    check(passes(placed, "owner_offset"), "a popup within a pixel of its place passes");
    check(passes(placed, "owner"), "and is a popup of its owner");
    check(find(placed, "minimized") == nullptr, "a popup has no state of its own to check");
    popup_mirror.placed = QPoint(100 + 134, 100 + 100);
    check(fails(expectation.against(popup_mirror, &owner_mirror), "owner_offset"),
          "a popup eight pixels off fails");
}

// Where the band around a window could not be looked at, Windows' rule
// says whether it is shaded, the desktop's setting included.
void an_unseen_shadow_follows_the_rule() {
    MirrorState shaded = faithful();
    shaded.flags &= ~Qt::NoDropShadowWindowHint;
    check(passes(checked(overlapped(), shaded, std::nullopt, true), "shadow"),
          "a DWM frame on a desktop with shadows is shaded");
    check(fails(checked(overlapped(), shaded, std::nullopt, false), "shadow"),
          "and is not where the desktop has them off");
    check(passes(checked(overlapped(), faithful(), std::nullopt, false), "shadow"),
          "so a mirror asking for none there passes");
    check(fails(checked(overlapped(), shaded, false, true), "shadow"),
          "what was seen outranks the rule");
}

void a_modal_dialog_is_one_whose_owner_is_disabled() {
    WindowTruth owner = overlapped();
    owner.enabled = false;
    WindowTruth dialog = overlapped();
    dialog.handle = 0x600aa;
    dialog.owner = owner.handle;
    MirrorState mirror = faithful();
    mirror.attached_to = owner.handle;
    mirror.flags = Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint;
    mirror.modal = true;
    const Expectation expectation{dialog, dialog, owner, false, true};
    check(passes(expectation.against(mirror, nullptr), "modal"), "a modal mirror over a disabled owner passes");
    check(passes(expectation.against(mirror, nullptr), "owner"), "attached to its owner is owned by it");
    check(passes(expectation.against(mirror, nullptr), "taskbar"), "and is off the taskbar");
    mirror.modal = false;
    check(fails(expectation.against(mirror, nullptr), "modal"), "one that does not know it is modal fails");
    owner.enabled = true;
    check(passes(Expectation{dialog, dialog, owner, false, true}.against(mirror, nullptr), "modal"),
          "a modeless dialog is not modal");
}

void the_foreground_window_asks_to_be_active() {
    WindowTruth foreground = overlapped();
    foreground.foreground = true;
    MirrorState mirror = faithful();
    check(fails(checked(foreground, mirror), "activation"), "a mirror that never asked fails");
    mirror.activation_requests = 1;
    check(passes(checked(foreground, mirror), "activation"), "one that asked passes, refused or not");
    mirror.activation_requests = 0;
    mirror.active = true;
    check(passes(checked(foreground, mirror), "activation"), "one that is active passes");
    foreground.extended_style |= 0x08000000;
    mirror.active = false;
    mirror.flags |= Qt::WindowDoesNotAcceptFocus;
    check(passes(checked(foreground, mirror), "activation"), "a WS_EX_NOACTIVATE one that never asked passes");
    mirror.activation_requests = 2;
    check(fails(checked(foreground, mirror), "activation"), "and fails once it asks");
    check(find(checked(overlapped(), faithful()), "activation") == nullptr,
          "a window that is not the foreground has no activation row");
}

void a_toplevel_is_where_the_guest_put_it() {
    const rwin::host::ScreenMapping screen{QPointF(0, 0), 1.2};
    MirrorState mirror = faithful();
    // 408,300 at 1.2.
    mirror.compositor_frame = QRectF(340, 250, 301.67, 230.83);
    const auto placed = Expectation{overlapped(), overlapped(), std::nullopt, false, true, screen}.against(mirror, nullptr);
    check(passes(placed, "host_position"), "a frame at the guest's place passes");
    mirror.compositor_frame = QRectF(345, 250, 301.67, 230.83);
    check(fails(Expectation{overlapped(), overlapped(), std::nullopt, false, true, screen}.against(mirror, nullptr),
                "host_position"),
          "six guest pixels off fails");
    mirror.compositor_frame.reset();
    const auto unknown = Expectation{overlapped(), overlapped(), std::nullopt, false, true, screen}.against(mirror, nullptr);
    const Expectation::Check* row = find(unknown, "host_position");
    check(row != nullptr && !row->passed && !row->known_limit.isEmpty(),
          "where the compositor does not say, a known limit");
    check(find(checked(overlapped(), faithful()), "host_position") == nullptr,
          "with no screen to map to, no row");
}

void rows_that_do_not_apply_are_left_out() {
    WindowTruth iconic = overlapped();
    iconic.iconic = true;
    MirrorState minimized = faithful();
    minimized.minimized = true;
    const auto rows = checked(iconic, minimized, std::nullopt);
    check(find(rows, "picture_size") == nullptr, "a minimised window has no picture to size");
    check(find(rows, "shadow") == nullptr, "nor a shadow that was looked for");
    check(passes(rows, "minimized"), "and is minimised on both sides");
}

}  // namespace

int main() {
    a_faithful_mirror_passes_every_row();
    topmost_wants_the_hint();
    the_taskbar_rule_is_windows_own();
    layering_is_read_from_the_attributes();
    a_mirror_that_moves_its_window_is_caught();
    an_owned_popup_is_placed_against_its_owner();
    an_unseen_shadow_follows_the_rule();
    a_modal_dialog_is_one_whose_owner_is_disabled();
    the_foreground_window_asks_to_be_active();
    a_toplevel_is_where_the_guest_put_it();
    rows_that_do_not_apply_are_left_out();

    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("expectation: all checks passed\n");
    return EXIT_SUCCESS;
}
