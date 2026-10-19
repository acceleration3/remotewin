#include "conform/expectation.hpp"

#include "host/chrome/win32_style.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace rwin::conform {

namespace {

using host::Win32Style;
using Check = Expectation::Check;
using Subject = Expectation::Subject;

QString yes_no(bool value) { return value ? QStringLiteral("yes") : QStringLiteral("no"); }

QString handle_text(std::uint64_t handle) {
    return handle == 0 ? QStringLiteral("none") : QStringLiteral("0x%1").arg(handle, 0, 16);
}

QString size_text(int width, int height) { return QStringLiteral("%1x%2").arg(width).arg(height); }

QString point_text(int x, int y) { return QStringLiteral("%1,%2").arg(x).arg(y); }

Check equal(QString expected, QString actual) {
    const bool passed = expected == actual;
    return Check{{}, {}, std::move(expected), std::move(actual), passed};
}

bool has_ex(const WindowTruth& truth, std::uint32_t bit) {
    return (truth.extended_style & bit) == bit;
}

bool on_screen(const WindowTruth& truth) { return truth.shown && !truth.iconic; }

// The guest pixels a mirror's picture covers: its device pixels, unfitted.
QSize shown_size(const host::MirrorState& mirror) {
    return QSize(static_cast<int>(std::lround(mirror.picture.width() / mirror.fit)),
                 static_cast<int>(std::lround(mirror.picture.height() / mirror.fit)));
}

QString placement(const WindowTruth& truth) {
    const QString state = !truth.shown   ? QStringLiteral("hidden")
                          : truth.iconic ? QStringLiteral("minimized")
                          : truth.zoomed ? QStringLiteral("maximized")
                                         : QStringLiteral("normal");
    return QStringLiteral("%1 at %2 %3")
        .arg(state, point_text(truth.window.left, truth.window.top),
             size_text(truth.window.width(), truth.window.height()));
}

// Nothing but the user changes a window's size, position or state, and
// the mirror only on the user's behalf: a window nobody touched is as it
// was made once its mirror is up.
std::optional<Check> undisturbed(const Subject& s) {
    return equal(placement(s.as_made), placement(s.truth));
}

std::optional<Check> shown(const Subject& s) {
    return equal(yes_no(s.truth.shown), yes_no(s.mirror.visible));
}

// A popup of the owner's mirror, or a window attached to it: either is
// kept above the owner by the compositor.
std::optional<Check> owner(const Subject& s) {
    return equal(handle_text(s.truth.owner),
                 handle_text(!s.mirror.popup_of.none() ? s.mirror.popup_of.hwnd : s.mirror.attached_to.hwnd));
}

// A window whose owner is disabled while it is enabled is that owner's
// modal dialog: what DialogBox and MessageBox make, and what a click on the
// owner brings forward instead (GW_ENABLEDPOPUP).
std::optional<Check> modal(const Subject& s) {
    if (s.owner == nullptr || !s.owner->exists || !s.truth.shown) return std::nullopt;
    const auto text = [](bool modal) { return modal ? QStringLiteral("modal over its owner") : QStringLiteral("not modal"); };
    return equal(text(!s.owner->enabled && s.truth.enabled), text(s.mirror.modal));
}

// The guest's foreground window is the one its user is in, and its mirror
// asks to be the active window here - asks: whether it is made active is
// the compositor's decision, which may refuse a client focus it was not
// given (focus stealing prevention). A WS_EX_NOACTIVATE window never asks.
std::optional<Check> activation(const Subject& s) {
    if (!s.truth.foreground || !on_screen(s.truth) || !s.mirror.popup_of.none()) return std::nullopt;
    if (has_ex(s.truth, Win32Style::kWsExNoActivate)) {
        return equal(QStringLiteral("never asked"), s.mirror.activation_requests == 0
                                                          ? QStringLiteral("never asked")
                                                          : QStringLiteral("asked %1 times").arg(s.mirror.activation_requests));
    }
    const QString actual = s.mirror.active                      ? QStringLiteral("active")
                           : s.mirror.activation_requests != 0 ? QStringLiteral("asked")
                                                                : QStringLiteral("never asked");
    Check check{{}, {}, QStringLiteral("active, or asked to be"), actual, actual != QLatin1String("never asked")};
    return check;
}

// Where the owned window's picture is against its owner's, in guest
// pixels: where the mirror asked for it, which is all a Wayland client
// decides. A popup is placed by whole logical units, so one guest pixel of
// difference is the rounding of that and not a misplacement; where the
// compositor then puts it is its own (docs/conformance.md, known limits).
std::optional<Check> owner_offset(const Subject& s) {
    if (s.owner == nullptr || s.owner_mirror == nullptr || s.mirror.popup_of.none()) {
        return std::nullopt;
    }
    if (!on_screen(s.truth)) return std::nullopt;
    const int expected_x = s.truth.visible.left - s.owner->visible.left;
    const int expected_y = s.truth.visible.top - s.owner->visible.top;
    const double ratio = s.mirror.device_pixel_ratio;
    const double fit = s.owner_mirror->fit;
    const QPointF device = QPointF(s.mirror.placed - s.owner_mirror->position) * ratio
                           + QPointF(s.mirror.picture.topLeft() - s.owner_mirror->picture.topLeft());
    const int actual_x = static_cast<int>(std::lround(device.x() / fit));
    const int actual_y = static_cast<int>(std::lround(device.y() / fit));
    const bool passed = std::abs(actual_x - expected_x) <= 1 && std::abs(actual_y - expected_y) <= 1;
    return Check{{}, {}, point_text(expected_x, expected_y), point_text(actual_x, actual_y), passed};
}

// "The Taskbar": a button for every visible window that is not owned and
// not a tool window; WS_EX_APPWINDOW forces one. WS_EX_NOACTIVATE: "does
// not appear on the taskbar by default".
std::optional<Check> taskbar(const Subject& s) {
    if (!s.truth.shown) return std::nullopt;
    bool expected = true;
    if (has_ex(s.truth, Win32Style::kWsExAppWindow)) {
        expected = true;
    } else if (has_ex(s.truth, Win32Style::kWsExToolWindow) || s.truth.owner != 0
               || has_ex(s.truth, Win32Style::kWsExNoActivate)) {
        expected = false;
    }
    const Qt::WindowFlags type = s.mirror.flags & Qt::WindowType_Mask;
    // A toplevel attached to another is left off the taskbar by the
    // desktop's task manager, which shows the window it is attached to.
    const bool actual = s.mirror.popup_of.none() && s.mirror.attached_to.none()
                        && (type == Qt::Window || type == Qt::Dialog);
    return equal(yes_no(expected), yes_no(actual));
}

std::optional<Check> stays_on_top(const Subject& s) {
    return equal(yes_no(has_ex(s.truth, Win32Style::kWsExTopMost)),
                 yes_no(s.mirror.flags.testFlag(Qt::WindowStaysOnTopHint)));
}

std::optional<Check> takes_focus(const Subject& s) {
    return equal(yes_no(!has_ex(s.truth, Win32Style::kWsExNoActivate)),
                 yes_no(!s.mirror.flags.testFlag(Qt::WindowDoesNotAcceptFocus)));
}

std::optional<Check> click_through(const Subject& s) {
    const bool expected = has_ex(s.truth, Win32Style::kWsExLayered)
                          && has_ex(s.truth, Win32Style::kWsExTransparent);
    return equal(yes_no(expected), yes_no(s.mirror.flags.testFlag(Qt::WindowTransparentForInput)));
}

std::optional<Check> takes_input(const Subject& s) {
    return equal(yes_no(s.truth.enabled), yes_no(s.mirror.forwards_input));
}

std::optional<Check> resizable(const Subject& s) {
    if (!on_screen(s.truth) || s.truth.zoomed) return std::nullopt;
    return equal(yes_no((s.truth.style & Win32Style::kWsThickFrame) != 0), yes_no(s.mirror.resizable));
}

std::optional<Check> minimized(const Subject& s) {
    if (!s.mirror.popup_of.none()) return std::nullopt;
    return equal(yes_no(s.truth.iconic), yes_no(s.mirror.minimized));
}

std::optional<Check> maximized(const Subject& s) {
    if (!s.mirror.popup_of.none()) return std::nullopt;
    return equal(yes_no(s.truth.zoomed), yes_no(s.mirror.maximized));
}

std::optional<Check> picture_size(const Subject& s) {
    if (!on_screen(s.truth)) return std::nullopt;
    const QSize shown = shown_size(s.mirror);
    return equal(size_text(s.truth.visible.width(), s.truth.visible.height()),
                 size_text(shown.width(), shown.height()));
}

std::optional<Check> picture_origin(const Subject& s) {
    if (!on_screen(s.truth) || s.truth.zoomed) return std::nullopt;
    return equal(point_text(0, 0), point_text(s.mirror.picture.left(), s.mirror.picture.top()));
}

std::optional<Check> one_to_one(const Subject& s) {
    if (!on_screen(s.truth) || s.truth.zoomed) return std::nullopt;
    return equal(QString::number(1.0, 'f', 3), QString::number(s.mirror.fit, 'f', 3));
}

std::optional<Check> opacity(const Subject& s) {
    const bool blended = has_ex(s.truth, Win32Style::kWsExLayered)
                         && (s.truth.layered_flags & Win32Style::kLwaAlpha) != 0;
    return equal(QString::number(blended ? s.truth.layered_alpha : 255),
                 QString::number(s.mirror.opacity));
}

std::optional<Check> color_key(const Subject& s) {
    const auto text = [](bool keyed, std::uint32_t colorref) {
        if (!keyed) return QStringLiteral("none");
        return QStringLiteral("#%1%2%3")
            .arg(colorref & 0xFF, 2, 16, QLatin1Char('0'))
            .arg((colorref >> 8) & 0xFF, 2, 16, QLatin1Char('0'))
            .arg((colorref >> 16) & 0xFF, 2, 16, QLatin1Char('0'));
    };
    const bool keyed = has_ex(s.truth, Win32Style::kWsExLayered)
                       && (s.truth.layered_flags & Win32Style::kLwaColorKey) != 0;
    return equal(text(keyed, s.truth.color_key), text(s.mirror.color_keyed, s.mirror.color_key));
}

std::optional<Check> shaped(const Subject& s) {
    const auto text = [](bool shaped) {
        return shaped ? QStringLiteral("shaped") : QStringLiteral("rectangular");
    };
    return equal(text(s.truth.region != WindowTruth::RegionKind::None), text(!s.mirror.mask.isEmpty()));
}

// Windows' rule: a CS_DROPSHADOW class, or a frame DWM draws - and neither
// when the desktop's "Show shadows under windows" is off.
bool shaded_by_rule(const Subject& s) {
    if (!s.drop_shadows) return false;
    constexpr std::uint32_t kFrame =
        Win32Style::kWsBorder | Win32Style::kWsDlgFrame | Win32Style::kWsThickFrame;
    return (s.truth.class_style & Win32Style::kCsDropShadow) != 0
           || (s.truth.dwm_frame && (s.truth.style & kFrame) != 0);
}

// What was seen on the guest's screen around the window - Windows' rule
// where too little of the band around it could be seen - against whether
// the mirror leaves the compositor free to shade it. That is all the host
// side can say: whether a frameless surface is given a shadow is the
// compositor's decision, and its output is not something a client reads.
std::optional<Check> shadow(const Subject& s) {
    if (!on_screen(s.truth) || s.truth.zoomed) return std::nullopt;
    const auto text = [](bool shadow) {
        return shadow ? QStringLiteral("shadow") : QStringLiteral("none");
    };
    return equal(text(s.casts_shadow.value_or(shaded_by_rule(s))),
                 text(!s.mirror.flags.testFlag(Qt::NoDropShadowWindowHint)));
}

// Where the guest put the window - CW_USEDEFAULT, SetWindowPos, a dialog
// centred over its owner, DS_CENTER - is where its mirror is on this
// desktop: the guest's screen is an output here (ScreenMapping), and the
// mirror's picture corner is its frame's. Within a guest pixel, which is
// less than a logical unit at any scale above one. A popup is placed
// against its owner (owner_offset); a maximised or minimised window by its
// state.
std::optional<Check> host_position(const Subject& s) {
    if (s.screen == nullptr || !on_screen(s.truth) || s.truth.zoomed || !s.mirror.popup_of.none()) {
        return std::nullopt;
    }
    const QString expected = point_text(s.truth.visible.left, s.truth.visible.top);
    if (!s.mirror.compositor_frame.has_value()) return Check{{}, {}, expected, QStringLiteral("not known"), false};
    const QPoint at = s.screen->guest_point(s.mirror.compositor_frame->topLeft());
    const bool passed = std::abs(at.x() - s.truth.visible.left) <= 1 && std::abs(at.y() - s.truth.visible.top) <= 1;
    return Check{{}, {}, expected, point_text(at.x(), at.y()), passed};
}

std::optional<Check> title(const Subject& s) {
    if (s.truth.title.empty()) return std::nullopt;
    return equal(QString::fromStdString(s.truth.title), s.mirror.title);
}

struct Row {
    const char* property;
    const char* rule;
    std::optional<Check> (*check)(const Subject&);
};

// Owned and WS_EX_APPWINDOW: Windows gives the window a taskbar button and
// keeps it where its owner put it. A Wayland client can have one or the
// other: an xdg_popup is placed against its parent and has no taskbar
// entry; an xdg_toplevel has one and cannot be placed at all.
QString taskbar_limit(const Subject& s) {
    if (s.truth.owner == 0 || !has_ex(s.truth, Win32Style::kWsExAppWindow)) return {};
    return QStringLiteral("an owned WS_EX_APPWINDOW window is a popup placed against its owner's mirror, "
                          "which has no taskbar entry on Wayland, or a window attached to it, which the "
                          "task manager leaves out for the window it is attached to");
}

// Windows hides an owner without hiding what it owns. A Wayland surface is
// placed against, or attached to, a surface that is mapped, and the
// owner's mirror is not: the window stands alone until its owner is back.
QString owner_limit(const Subject& s) {
    if (s.owner == nullptr || s.owner->shown) return {};
    return QStringLiteral("its owner is hidden, and a Wayland surface can only be placed against or "
                          "attached to one that is mapped");
}

// Wayland gives a client neither a place for its window nor word of where
// the compositor put it; only KWin, through its scripting, does both.
QString host_position_limit(const Subject& s) {
    if (s.mirror.compositor_frame.has_value()) return {};
    return QStringLiteral("the compositor does not say where it put the window (only KWin's scripting does), "
                          "so the mirror is placed where the compositor chooses");
}

// Why a failed row is what this platform allows, or nothing.
QString known_limit(const QString& property, const Subject& s) {
    if (property == QLatin1String("taskbar")) return taskbar_limit(s);
    if (property == QLatin1String("owner")) return owner_limit(s);
    if (property == QLatin1String("host_position")) return host_position_limit(s);
    return {};
}

constexpr std::array kRows{
    Row{"undisturbed", "a window nobody touched keeps the size, place and state it was made with",
        &undisturbed},
    Row{"shown", "IsWindowVisible: a visible window is on screen", &shown},
    Row{"owner", "GW_OWNER: an owned window is kept above its owner, so it is shown as part of it",
        &owner},
    Row{"owner_offset", "an owned window is where its owner put it, relative to the owner",
        &owner_offset},
    Row{"modal",
        "DialogBox, MessageBox: the owner is disabled while its modal dialog is up, and a click on it "
        "brings the dialog forward",
        &modal},
    Row{"activation",
        "SetForegroundWindow, a dialog opening, a dialog closing back to its owner: the foreground "
        "window is the active one (WS_EX_NOACTIVATE: never activated)",
        &activation},
    Row{"taskbar",
        "The Taskbar: unowned windows get a button; WS_EX_TOOLWINDOW and WS_EX_NOACTIVATE do not, "
        "WS_EX_APPWINDOW always does",
        &taskbar},
    Row{"stays_on_top", "WS_EX_TOPMOST: above all non-topmost windows, even deactivated",
        &stays_on_top},
    Row{"takes_focus", "WS_EX_NOACTIVATE: does not become the foreground window when clicked",
        &takes_focus},
    Row{"click_through",
        "WS_EX_LAYERED | WS_EX_TRANSPARENT: mouse events pass to the windows underneath",
        &click_through},
    Row{"takes_input", "WS_DISABLED: a disabled window cannot receive input from the user",
        &takes_input},
    Row{"resizable", "WS_THICKFRAME: the window has a sizing border", &resizable},
    Row{"minimized", "IsIconic", &minimized},
    Row{"maximized", "IsZoomed", &maximized},
    Row{"picture_size",
        "DWMWA_EXTENDED_FRAME_BOUNDS: the visible frame, one guest pixel per device pixel",
        &picture_size},
    Row{"picture_origin", "the window's visible edge is the mirror's edge", &picture_origin},
    Row{"fit", "a window that is not maximised is shown one to one", &one_to_one},
    Row{"opacity", "LWA_ALPHA: the whole window is blended with this constant alpha", &opacity},
    Row{"color_key", "LWA_COLORKEY: every pixel painted in this colour is transparent",
        &color_key},
    Row{"shape", "SetWindowRgn: only the region is drawn, and only it is hit", &shaped},
    Row{"shadow",
        "a shadow drawn around the window on the guest's screen (where it cannot be seen: "
        "CS_DROPSHADOW or a DWM frame, unless SPI_GETDROPSHADOW is off)",
        &shadow},
    Row{"host_position",
        "the window rectangle (its visible frame): where the guest put it is where its mirror is, "
        "the guest's screen being this desktop's output",
        &host_position},
    Row{"title", "GetWindowText", &title},
};

}  // namespace

Expectation::Expectation(WindowTruth as_made, WindowTruth truth, std::optional<WindowTruth> owner,
                         std::optional<bool> casts_shadow, bool drop_shadows,
                         std::optional<host::ScreenMapping> screen)
    : as_made_(std::move(as_made)),
      truth_(std::move(truth)),
      owner_(std::move(owner)),
      casts_shadow_(casts_shadow),
      drop_shadows_(drop_shadows),
      screen_(screen) {}

std::vector<Check> Expectation::against(const host::MirrorState& mirror,
                                        const host::MirrorState* owner_mirror) const {
    const Subject subject{as_made_, truth_, owner_.has_value() ? &*owner_ : nullptr, casts_shadow_,
                          drop_shadows_, mirror, owner_mirror, screen_.has_value() ? &*screen_ : nullptr};
    std::vector<Check> checks;
    checks.reserve(kRows.size());
    for (const Row& row : kRows) {
        std::optional<Check> check = row.check(subject);
        if (!check.has_value()) continue;
        check->property = QString::fromLatin1(row.property);
        check->rule = QString::fromLatin1(row.rule);
        if (!check->passed) check->known_limit = known_limit(check->property, subject);
        checks.push_back(std::move(*check));
    }
    return checks;
}

}  // namespace rwin::conform
