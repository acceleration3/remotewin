#include "conform/pointer_expectation.hpp"

#include "conform/cursor_picture_on_glass.hpp"
#include "conform/pixel_comparison.hpp"
#include "proto/system_cursor.hpp"

#include <QStringList>

#include <array>
#include <cmath>
#include <cstdlib>

namespace rwin::conform {

namespace {

using Check = Expectation::Check;
using Sides = PointerExpectation::Sides;
using Kind = PointerGrab::Kind;

struct NativeShape {
    std::uint32_t idc;
    Qt::CursorShape shape;
    const char* name;
};

// This desktop's cursor of the same meaning, by the CSS cursor names
// wp_cursor_shape_v1 draws: written out here, not asked of the mirror's
// own table (native_shape_for), which a check has to be able to catch.
constexpr std::array kNativeShapes{
    NativeShape{32512, Qt::ArrowCursor, "default"},       NativeShape{32513, Qt::IBeamCursor, "text"},
    NativeShape{32514, Qt::WaitCursor, "wait"},           NativeShape{32515, Qt::CrossCursor, "crosshair"},
    NativeShape{32516, Qt::UpArrowCursor, "n-resize"},    NativeShape{32642, Qt::SizeFDiagCursor, "nwse-resize"},
    NativeShape{32643, Qt::SizeBDiagCursor, "nesw-resize"}, NativeShape{32644, Qt::SizeHorCursor, "ew-resize"},
    NativeShape{32645, Qt::SizeVerCursor, "ns-resize"},   NativeShape{32646, Qt::SizeAllCursor, "all-scroll"},
    NativeShape{32648, Qt::ForbiddenCursor, "not-allowed"}, NativeShape{32649, Qt::PointingHandCursor, "pointer"},
    NativeShape{32650, Qt::BusyCursor, "progress"},       NativeShape{32651, Qt::WhatsThisCursor, "help"},
};

const NativeShape* native_for(std::uint32_t idc) {
    for (const NativeShape& native : kNativeShapes) {
        if (native.idc == idc) return &native;
    }
    return nullptr;
}

QString shape_text(Qt::CursorShape shape) {
    if (shape == Qt::BlankCursor) return QStringLiteral("none (blank)");
    if (shape == Qt::BitmapCursor) return QStringLiteral("pixels");
    for (const NativeShape& native : kNativeShapes) {
        if (native.shape == shape) return QStringLiteral("this desktop's %1").arg(QLatin1String(native.name));
    }
    return QStringLiteral("Qt shape %1").arg(static_cast<int>(shape));
}

bool own_pixels(const CursorTruth& cursor) {
    return cursor.shown() && !cursor.frames.empty() && native_for(cursor.system_idc) == nullptr;
}

// The guest's picture at the mirror's step of it.
const CursorTruth::Frame& guest_frame(const Sides& s) {
    const std::size_t step = s.cursor.frames.size() > 1 && !s.pointer_mirror.cursor_delays_ms.empty()
                                 ? s.pointer_mirror.cursor_steps_taken % s.cursor.frames.size()
                                 : 0;
    return s.cursor.frames[step];
}

std::optional<Check> cursor_hidden(const Sides& s) {
    const auto text = [](bool hidden) { return hidden ? QStringLiteral("hidden") : QStringLiteral("shown"); };
    return Check{{}, {}, text(!s.cursor.shown()), text(s.pointer_mirror.cursor_shape == Qt::BlankCursor),
                 s.cursor.shown() == (s.pointer_mirror.cursor_shape != Qt::BlankCursor)};
}

std::optional<Check> cursor_shape_native(const Sides& s, const NativeShape& native) {
    const QString expected = QStringLiteral("%1: this desktop's %2")
                                 .arg(QLatin1String(name_of(system_cursor_of(native.idc))), QLatin1String(native.name));
    const QString actual = shape_text(s.pointer_mirror.cursor_shape);
    return Check{{}, {}, expected, actual, s.pointer_mirror.cursor_shape == native.shape};
}

std::optional<Check> compare_pixels(const Sides& s, bool over_white) {
    const CursorPictureOnGlass guest = CursorPictureOnGlass::of_guest(guest_frame(s), over_white);
    const std::optional<CursorPictureOnGlass> mirror = CursorPictureOnGlass::of_mirror(s.pointer_mirror, over_white);
    const QString expected = QStringLiteral("the guest's %1x%2 picture over %3")
                                 .arg(guest.image().width()).arg(guest.image().height())
                                 .arg(over_white ? QStringLiteral("white") : QStringLiteral("black"));
    if (!mirror.has_value()) return Check{{}, {}, expected, shape_text(s.pointer_mirror.cursor_shape), false};
    const QImage guest_canvas = guest.aligned_with(*mirror);
    const QImage mirror_canvas = mirror->aligned_with(guest);
    const PixelComparison pixels(guest_canvas, mirror_canvas, QRegion(guest_canvas.rect()), s.tolerance);
    return Check{{}, {}, expected,
                 QStringLiteral("%1 of %2 differ, max delta %3").arg(pixels.mismatched()).arg(pixels.compared()).arg(pixels.max_delta()),
                 pixels.matches()};
}

// DefWindowProc's answer to WM_SETCURSOR over a sizing border (HTLEFT ...
// HTBOTTOMRIGHT): the size cursor along it.
std::uint32_t idc_for_edges(Qt::Edges edges) {
    const bool left = edges.testFlag(Qt::LeftEdge);
    const bool right = edges.testFlag(Qt::RightEdge);
    const bool top = edges.testFlag(Qt::TopEdge);
    const bool bottom = edges.testFlag(Qt::BottomEdge);
    if ((left && top) || (right && bottom)) return idc_of(SystemCursor::SizeNwse);
    if ((right && top) || (left && bottom)) return idc_of(SystemCursor::SizeNesw);
    if (left || right) return idc_of(SystemCursor::SizeWe);
    return idc_of(SystemCursor::SizeNs);
}

// Over the mirror's resize band, which stands for the window's sizing
// border outside its visible frame, the cursor is the one Windows shows on
// that border; anywhere else, the guest's.
std::optional<Check> cursor_shape(const Sides& s) {
    if (s.pointer_mirror.hovered_edges != Qt::Edges{}) {
        Check check = *cursor_shape_native(s, *native_for(idc_for_edges(s.pointer_mirror.hovered_edges)));
        check.expected += QStringLiteral(" (on the sizing border the mirror's resize band stands for)");
        return check;
    }
    if (!s.cursor.shown() || s.cursor.frames.empty()) return std::nullopt;
    if (const NativeShape* native = native_for(s.cursor.system_idc)) return cursor_shape_native(s, *native);
    return compare_pixels(s, true);
}

// Captured, the window's cursor stays as the pointer leaves it: Windows
// sends WM_SETCURSOR to no other window meanwhile.
std::optional<Check> cursor_outside(const Sides& s) {
    if (s.cursor.capture == 0 || s.cursor.under == s.pointer_window.handle || !s.cursor.shown()) return std::nullopt;
    return Check{{}, {}, QStringLiteral("the capturing window's cursor, outside it too"),
                 QStringLiteral("this desktop's own cursor outside the mirror"), false};
}

std::optional<Check> cursor_over_dark(const Sides& s) {
    if (!own_pixels(s.cursor)) return std::nullopt;
    return compare_pixels(s, false);
}

// The pixel the pointer is at, as a place in the picture: where the drawn
// part is around it, on each side.
std::optional<Check> cursor_hotspot(const Sides& s) {
    if (!own_pixels(s.cursor)) return std::nullopt;
    const CursorTruth::Frame& frame = guest_frame(s);
    const QRect guest = CursorPictureOnGlass::of_guest(frame, true).drawn_around_hotspot()
                        | CursorPictureOnGlass::of_guest(frame, false).drawn_around_hotspot();
    const std::optional<CursorPictureOnGlass> white = CursorPictureOnGlass::of_mirror(s.pointer_mirror, true);
    const std::optional<CursorPictureOnGlass> black = CursorPictureOnGlass::of_mirror(s.pointer_mirror, false);
    const auto text = [&](const QRect& drawn) {
        return QStringLiteral("pixel %1,%2 of the drawn %3x%4").arg(-drawn.left()).arg(-drawn.top()).arg(drawn.width()).arg(drawn.height());
    };
    if (!white.has_value() || !black.has_value()) {
        return Check{{}, {}, text(guest), shape_text(s.pointer_mirror.cursor_shape), false};
    }
    const QRect mirror = white->drawn_around_hotspot() | black->drawn_around_hotspot();
    return Check{{}, {}, text(guest), text(mirror), guest.topLeft() == mirror.topLeft()};
}

std::optional<Check> cursor_size(const Sides& s) {
    if (!own_pixels(s.cursor)) return std::nullopt;
    const std::optional<CursorPictureOnGlass> mirror = CursorPictureOnGlass::of_mirror(s.pointer_mirror, true);
    if (!mirror.has_value()) return Check{{}, {}, QStringLiteral("1.000"), shape_text(s.pointer_mirror.cursor_shape), false};
    const QString how = s.pointer_mirror.cursor_surface_size.has_value()
                            ? QStringLiteral(" (surface %1x%2 at %3)")
                                  .arg(s.pointer_mirror.cursor_surface_size->width())
                                  .arg(s.pointer_mirror.cursor_surface_size->height())
                                  .arg(s.pointer_mirror.device_pixel_ratio, 0, 'f', 2)
                            : QStringLiteral(" (pixmap at ratio %1, resampled)").arg(s.pointer_mirror.cursor_pixmap_ratio, 0, 'f', 2);
    return Check{{}, {}, QString::number(1.0, 'f', 3), QString::number(mirror->scale(), 'f', 3) + how,
                 std::abs(mirror->scale() - 1.0) < 1e-3};
}

QString delays_text(const std::vector<int>& delays) {
    QStringList parts;
    for (const int delay : delays) parts << QString::number(delay);
    return parts.join(QLatin1Char('/')) + QStringLiteral(" ms");
}

std::optional<Check> cursor_animated(const Sides& s) {
    if (!own_pixels(s.cursor) || s.cursor.frames.size() < 2) return std::nullopt;
    std::vector<int> guest;
    for (const CursorTruth::Frame& frame : s.cursor.frames) guest.push_back(static_cast<int>(frame.delay_ms));
    const std::vector<int>& mirror = s.pointer_mirror.cursor_delays_ms;
    // A step's time is in sixtieths of a second on both sides; a
    // millisecond either way is the rounding of that.
    bool same = mirror.size() == guest.size();
    for (std::size_t i = 0; same && i < guest.size(); ++i) same = std::abs(mirror[i] - guest[i]) <= 1;
    const bool stepping = s.pointer_mirror.cursor_steps_taken > 0;
    return Check{{}, {}, QStringLiteral("%1 steps of %2, stepping").arg(guest.size()).arg(delays_text(guest)),
                 QStringLiteral("%1 steps of %2, %3 steps shown").arg(mirror.size()).arg(delays_text(mirror))
                     .arg(s.pointer_mirror.cursor_steps_taken),
                 same && stepping};
}

QString kind_text(Kind kind) {
    switch (kind) {
        case Kind::Confine:
            return QStringLiteral("confined");
        case Kind::Lock:
            return QStringLiteral("locked");
        case Kind::None:
            break;
    }
    return QStringLiteral("nothing held");
}

// Why the user here let go: what the harness did, or the mirror losing the
// focus to something else on this desktop - as Qt says it is not active, not
// only as the mirror says it let go.
QString let_go_of(const Sides& s) {
    if (!s.let_go.isEmpty()) return s.let_go;
    const host::MirrorState* mirror = s.foreground_mirror;
    if (mirror != nullptr && !mirror->active && mirror->pointer_hold.has_value() && mirror->pointer_hold->lost_focus) {
        return QStringLiteral("the mirror lost the focus to another window here");
    }
    return {};
}

// What is to be held: what the guest wants, unless the user here let go.
Kind expected_hold(const Sides& s, Kind wanted) { return let_go_of(s).isEmpty() ? wanted : Kind::None; }

QString expected_text(const Sides& s, Kind wanted) {
    const QString let_go = let_go_of(s);
    if (wanted != Kind::None && !let_go.isEmpty()) return QStringLiteral("nothing held: %1").arg(let_go);
    return kind_text(wanted);
}

Kind asked(const Sides& s) {
    if (s.foreground_mirror == nullptr || !s.foreground_mirror->pointer_hold.has_value()) return Kind::None;
    return s.foreground_mirror->pointer_hold->asked;
}

QString hold_text(const Sides& s) {
    if (s.foreground_mirror == nullptr || !s.foreground_mirror->pointer_hold.has_value()) return kind_text(Kind::None);
    const host::MirrorState::PointerHold& hold = *s.foreground_mirror->pointer_hold;
    if (hold.asked == Kind::None) {
        return hold.unavailable.isEmpty() ? kind_text(Kind::None)
                                          : QStringLiteral("nothing asked: %1").arg(hold.unavailable);
    }
    QString text = QStringLiteral("%1 asked, %2").arg(kind_text(hold.asked),
                                                      hold.engaged ? QStringLiteral("in force") : QStringLiteral("not yet in force"));
    if (hold.asked == Kind::Confine) {
        const QRect box = hold.region.boundingRect();
        text += QStringLiteral(", to %1,%2 %3x%4").arg(box.x()).arg(box.y()).arg(box.width()).arg(box.height());
    }
    return text;
}

// The clip within the foreground window's picture, in the mirror's logical
// units, rounded inward: where the mirror's pointer may go and be over the
// guest's clip.
std::optional<QRect> expected_region(const Sides& s) {
    if (s.foreground == nullptr || s.foreground_mirror == nullptr) return std::nullopt;
    const WindowTruth::Rect& clip = s.cursor.clip;
    const WindowTruth::Rect& visible = s.foreground->visible;
    const QRect inside = QRect(QPoint(clip.left, clip.top), QPoint(clip.right - 1, clip.bottom - 1))
                         & QRect(QPoint(visible.left, visible.top), QPoint(visible.right - 1, visible.bottom - 1));
    if (inside.isEmpty()) return std::nullopt;
    const host::MirrorState& mirror = *s.foreground_mirror;
    const auto logical = [&](QPoint guest) {
        return (QPointF(guest - QPoint(visible.left, visible.top)) * mirror.fit + QPointF(mirror.picture.topLeft()))
               / mirror.device_pixel_ratio;
    };
    const QPointF top_left = logical(inside.topLeft());
    const QPointF bottom_right = logical(inside.bottomRight() + QPoint(1, 1));
    return QRect(QPoint(static_cast<int>(std::ceil(top_left.x())), static_cast<int>(std::ceil(top_left.y()))),
                 QPoint(static_cast<int>(std::floor(bottom_right.x())) - 1, static_cast<int>(std::floor(bottom_right.y())) - 1));
}

QString unavailable_of(const Sides& s) {
    if (s.foreground_mirror == nullptr || !s.foreground_mirror->pointer_hold.has_value()) return {};
    return s.foreground_mirror->pointer_hold->unavailable;
}

std::optional<Check> confine(const Sides& s, Kind wanted) {
    const Kind hold = expected_hold(s, wanted);
    if (wanted != Kind::Confine && asked(s) != Kind::Confine) return std::nullopt;
    const std::optional<QRect> region = hold == Kind::Confine ? expected_region(s) : std::nullopt;
    const QString expected = region.has_value()
                                 ? QStringLiteral("confined, to %1,%2 %3x%4").arg(region->x()).arg(region->y())
                                       .arg(region->width()).arg(region->height())
                                 : expected_text(s, wanted);
    Check check{{}, {}, expected, hold_text(s), false};
    if (asked(s) != hold) return check;
    if (hold == Kind::None) {
        check.passed = true;
        return check;
    }
    const QRect box = s.foreground_mirror->pointer_hold->region.boundingRect();
    check.passed = region.has_value() && std::abs(box.left() - region->left()) <= 1 && std::abs(box.top() - region->top()) <= 1
                   && std::abs(box.right() - region->right()) <= 1 && std::abs(box.bottom() - region->bottom()) <= 1;
    return check;
}

// Also where nothing is to be held but the cursor is hidden: a hidden
// cursor alone is no lock.
std::optional<Check> pointer_lock(const Sides& s, Kind wanted) {
    const Kind hold = expected_hold(s, wanted);
    if (wanted != Kind::Lock && asked(s) != Kind::Lock && s.cursor.shown()) return std::nullopt;
    if (wanted == Kind::Confine) return std::nullopt;
    return Check{{}, {}, expected_text(s, wanted), hold_text(s), (hold == Kind::Lock) == (asked(s) == Kind::Lock)};
}

std::optional<Check> release(const Sides& s, Kind wanted) {
    if (expected_hold(s, wanted) != Kind::None || (s.held_before == Kind::None && wanted == Kind::None)) return std::nullopt;
    return Check{{}, {}, expected_text(s, wanted), hold_text(s), asked(s) == Kind::None};
}

// A hidden cursor read raw and neither clipped nor put back: what a game
// with RIDEV_NOLEGACY does, and what a video player hiding its cursor looks
// like from any other process.
bool raw_input_only(const Sides& s) {
    return !s.cursor.shown() && s.cursor.raw_mouse && !s.cursor.clipped
           && s.cursor.put_back < PointerExpectation::kPutBackToLook;
}

QString known_limit(const QString& property, const Sides& s) {
    if (property == QLatin1String("cursor_outside")) {
        return QStringLiteral("a Wayland client's cursor shows only while the pointer is over one of its surfaces, or "
                              "held to one by a button pressed on it; a capture without a button has no counterpart");
    }
    if (property == QLatin1String("pointer_lock") && raw_input_only(s)) {
        return QStringLiteral("no process can read another's raw input registration, and a hidden cursor that is "
                              "neither clipped nor put back is what a video player's is too: the guest half cannot "
                              "tell this game from one");
    }
    const QString unavailable = unavailable_of(s);
    if ((property == QLatin1String("pointer_lock") || property == QLatin1String("confine")) && !unavailable.isEmpty()) {
        return QStringLiteral("nothing could be asked of the compositor: %1").arg(unavailable);
    }
    if (property == QLatin1String("cursor_over_dark") && s.pointer_mirror.cursor_inverts) {
        return QStringLiteral("the cursor has pixels that invert what is under them; a Wayland cursor is a picture "
                              "with alpha and nothing composes it as an inversion, so those pixels are black edged "
                              "in white - right over white, visible but not inverted over black");
    }
    return {};
}

struct Row {
    const char* property;
    const char* rule;
    std::optional<Check> (*check)(const Sides&);
};

constexpr std::array kCursorRows{
    Row{"cursor_hidden",
        "GetCursorInfo: a cursor is shown while CURSOR_SHOWING, not suppressed, with a cursor set "
        "(ShowCursor's count below 0, or SetCursor(NULL), shows none)",
        &cursor_hidden},
    Row{"cursor_shape",
        "the cursor WM_SETCURSOR or the class set: a system cursor (LoadCursor IDC_*) is this desktop's own of the "
        "same meaning; any other is the guest's pixels, as DrawIconEx draws them over white",
        &cursor_shape},
    Row{"cursor_over_dark", "the guest's pixels as DrawIconEx draws them over black", &cursor_over_dark},
    Row{"cursor_hotspot", "ICONINFO's hotspot: the pixel the pointer is at", &cursor_hotspot},
    Row{"cursor_size", "the cursor's own pixels, one to one on the screen at any scale", &cursor_size},
    Row{"cursor_animated",
        "an animated cursor (.ani): every step, each for its own time (GetCursorFrameInfo), round and round",
        &cursor_animated},
    Row{"cursor_outside", "SetCapture: the capturing window's cursor stays as the pointer leaves it", &cursor_outside},
};

}  // namespace

PointerGrab::Kind PointerExpectation::wanted() const {
    const CursorTruth& cursor = sides_.cursor;
    if (sides_.foreground == nullptr) return Kind::None;
    const bool hidden = !cursor.shown();
    const bool tiny = cursor.clipped && cursor.clip.width() <= kTinyClip && cursor.clip.height() <= kTinyClip;
    const bool raw = cursor.raw_mouse && (cursor.raw_target == 0 || cursor.raw_target == sides_.foreground->handle);
    // Seen to be put back as many times in a row as it takes anyone
    // watching to call it that (PointerGrabRule::kReturnsToWarp): before
    // that no other process can know, and after it stops, not until a move
    // is left alone.
    const bool recentred = cursor.put_back_by == sides_.foreground->handle && cursor.put_back >= kPutBackToLook;
    if (tiny || (hidden && (cursor.clipped || recentred || raw))) return Kind::Lock;
    return cursor.clipped ? Kind::Confine : Kind::None;
}

std::vector<Check> PointerExpectation::checks() const {
    std::vector<Check> checks;
    const auto add = [&](const char* property, const char* rule, std::optional<Check> check) {
        if (!check.has_value()) return;
        check->property = QString::fromLatin1(property);
        check->rule = QString::fromLatin1(rule);
        if (!check->passed) check->known_limit = known_limit(check->property, sides_);
        checks.push_back(std::move(*check));
    };
    for (const Row& row : kCursorRows) add(row.property, row.rule, row.check(sides_));
    const Kind want = wanted();
    add("confine",
        "ClipCursor with the cursor shown, in the foreground: the pointer stays within the clip, here the part of the "
        "mirror that shows it",
        confine(sides_, want));
    add("pointer_lock",
        "mouse look - a hidden cursor clipped (ClipCursor), put back on every move (SetCursorPos), or read raw "
        "(RegisterRawInputDevices), or a clip too small to move in: the pointer is held and only its motion passed on",
        pointer_lock(sides_, want));
    add("release",
        "ClipCursor(NULL), the cursor shown again, another application in front: the pointer is let go", release(sides_, want));
    return checks;
}

}  // namespace rwin::conform
