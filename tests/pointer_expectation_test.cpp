#include "conform/cursor_picture_on_glass.hpp"
#include "conform/motion_expectation.hpp"
#include "conform/pointer_expectation.hpp"

#include <QPainter>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

using rwin::CursorTruth;
using rwin::InputRecord;
using rwin::PointerGrab;
using rwin::WindowTruth;
using rwin::conform::CursorPictureOnGlass;
using rwin::conform::Expectation;
using rwin::conform::MotionExpectation;
using rwin::conform::PointerExpectation;
using rwin::host::MirrorState;
using Kind = PointerGrab::Kind;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

constexpr std::uint64_t kGame = 0x500c4;
constexpr std::uint32_t kShowing = 1;

WindowTruth game() {
    WindowTruth truth;
    truth.handle = kGame;
    truth.foreground = true;
    truth.visible = {408, 300, 770, 577};
    truth.client = {409, 336, 769, 576};
    return truth;
}

CursorTruth cursor(bool shown) {
    CursorTruth truth;
    truth.flags = shown ? kShowing : 0;
    truth.handle = shown ? 0x10003 : 0;
    truth.system_idc = shown ? 32512 : 0;
    truth.clip = {0, 0, 2560, 1440};
    if (shown) truth.frames.push_back(CursorTruth::Frame{32, 32, 0, 0, 0, {}, {}});
    return truth;
}

MirrorState mirror(Qt::CursorShape shape) {
    MirrorState state;
    state.cursor_shape = shape;
    state.device_pixel_ratio = 1.2;
    state.picture = QRect(0, 0, 362, 277);
    return state;
}

const Expectation::Check* row(const std::vector<Expectation::Check>& checks, const char* property) {
    const auto found = std::find_if(checks.begin(), checks.end(),
                                    [&](const Expectation::Check& check) { return check.property == QLatin1String(property); });
    return found == checks.end() ? nullptr : &*found;
}

Kind wanted_of(const CursorTruth& truth, const WindowTruth* foreground = nullptr) {
    const WindowTruth window = game();
    const MirrorState state = mirror(Qt::ArrowCursor);
    return PointerExpectation{{truth, window, foreground, state, nullptr}}.wanted();
}

void what_is_wanted_is_read_off_windows() {
    const WindowTruth window = game();
    CursorTruth hidden = cursor(false);
    check(wanted_of(hidden, &window) == Kind::None, "a hidden cursor alone wants nothing (a video player)");
    CursorTruth clipped = cursor(true);
    clipped.clipped = true;
    clipped.clip = {409, 336, 769, 576};
    check(wanted_of(clipped, &window) == Kind::Confine, "a shown cursor clipped is confined");
    hidden.clipped = true;
    hidden.clip = clipped.clip;
    check(wanted_of(hidden, &window) == Kind::Lock, "a hidden cursor clipped is mouse look");
    check(wanted_of(hidden) == Kind::None, "nothing is held for an application that is not in front");
    CursorTruth put_back = cursor(false);
    put_back.put_back_by = kGame;
    put_back.put_back = PointerExpectation::kPutBackToLook - 1;
    check(wanted_of(put_back, &window) == Kind::None, "a cursor put back twice is not yet seen to be held");
    put_back.put_back = PointerExpectation::kPutBackToLook;
    check(wanted_of(put_back, &window) == Kind::Lock, "three times is mouse look");
    CursorTruth tiny = cursor(true);
    tiny.clipped = true;
    tiny.clip = {500, 400, 501, 401};
    check(wanted_of(tiny, &window) == Kind::Lock, "a one-pixel clip holds even a shown cursor");
    CursorTruth raw = cursor(false);
    raw.raw_mouse = true;
    check(wanted_of(raw, &window) == Kind::Lock, "a hidden cursor read raw is mouse look");
}

void the_cursor_rows_compare_what_is_worn() {
    const WindowTruth window = game();
    const CursorTruth hidden = cursor(false);
    const MirrorState blank = mirror(Qt::BlankCursor);
    auto checks = PointerExpectation{{hidden, window, &window, blank, nullptr}}.checks();
    check(row(checks, "cursor_hidden") != nullptr && row(checks, "cursor_hidden")->passed, "hidden is worn as blank");
    const MirrorState arrow = mirror(Qt::ArrowCursor);
    checks = PointerExpectation{{hidden, window, &window, arrow, nullptr}}.checks();
    check(!row(checks, "cursor_hidden")->passed, "and an arrow over a hidden cursor is caught");

    CursorTruth ibeam = cursor(true);
    ibeam.system_idc = 32513;
    const MirrorState text = mirror(Qt::IBeamCursor);
    checks = PointerExpectation{{ibeam, window, &window, text, nullptr}}.checks();
    check(row(checks, "cursor_shape")->passed, "IDC_IBEAM is this desktop's text cursor");
    checks = PointerExpectation{{ibeam, window, &window, arrow, nullptr}}.checks();
    check(!row(checks, "cursor_shape")->passed, "and not its arrow");

    MirrorState band = mirror(Qt::SizeHorCursor);
    band.hovered_edges = Qt::RightEdge;
    checks = PointerExpectation{{cursor(true), window, &window, band, nullptr}}.checks();
    check(row(checks, "cursor_shape")->passed, "over the resize band the cursor is the sizing border's, IDC_SIZEWE");
}

void holds_are_let_go() {
    const WindowTruth window = game();
    CursorTruth hidden = cursor(false);
    hidden.clipped = true;
    hidden.clip = {409, 336, 769, 576};
    MirrorState held = mirror(Qt::BlankCursor);
    held.pointer_hold = MirrorState::PointerHold{Kind::Lock, Kind::Lock};
    auto checks = PointerExpectation{{hidden, window, &window, held, &held}}.checks();
    check(row(checks, "pointer_lock") != nullptr && row(checks, "pointer_lock")->passed, "mouse look is held");
    checks = PointerExpectation{{hidden, window, &window, held, &held, Kind::None, QStringLiteral("the mirror lost the focus")}}
                 .checks();
    check(!row(checks, "release")->passed, "held after the user let go is caught");
    MirrorState let_go = held;
    let_go.pointer_hold->asked = Kind::None;
    checks = PointerExpectation{{hidden, window, &window, let_go, &let_go, Kind::Lock, QStringLiteral("the mirror lost the focus")}}
                 .checks();
    check(row(checks, "release")->passed, "let go while the mirror does not have the focus");
    MirrorState unfocused = let_go;
    unfocused.pointer_hold->lost_focus = true;
    checks = PointerExpectation{{hidden, window, &window, unfocused, &unfocused, Kind::Lock}}.checks();
    check(row(checks, "release") != nullptr && row(checks, "release")->passed,
          "let go when the mirror lost the focus to another window, as Qt says it is not active");
    unfocused.active = true;
    checks = PointerExpectation{{hidden, window, &window, unfocused, &unfocused, Kind::Lock}}.checks();
    check(!row(checks, "pointer_lock")->passed, "but not when Qt says it still has the focus");
    const CursorTruth shown = cursor(true);
    const MirrorState free = mirror(Qt::ArrowCursor);
    checks = PointerExpectation{{shown, window, &window, free, &free, Kind::Lock}}.checks();
    check(row(checks, "release") != nullptr && row(checks, "release")->passed, "and let go when the game shows its cursor");

    CursorTruth raw = cursor(false);
    raw.raw_mouse = true;
    checks = PointerExpectation{{raw, window, &window, free, &free}}.checks();
    check(row(checks, "pointer_lock") != nullptr && !row(checks, "pointer_lock")->passed
              && !row(checks, "pointer_lock")->known_limit.isEmpty(),
          "raw input no other process can see is a known limit, not a pass");
}

void a_padded_cursor_on_the_glass_is_the_guest_s() {
    CursorTruth::Frame guest{4, 4, 1, 1, 0, {}, {}};
    for (int i = 0; i < 16; ++i) {
        const bool red = i % 5 == 0;
        for (const std::uint8_t byte : {std::uint8_t{0}, std::uint8_t{0}, static_cast<std::uint8_t>(red ? 255 : 0), std::uint8_t{255}}) {
            guest.over_white.push_back(byte);
            guest.over_black.push_back(byte);
        }
    }
    // At 1.2 a hotspot at 1 is padded by 5 to land on 6: 12 pixels, 10 units.
    QImage pixels(12, 12, QImage::Format_ARGB32);
    pixels.fill(Qt::transparent);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) pixels.setPixel(5 + x, 5 + y, (y * 4 + x) % 5 == 0 ? qRgb(255, 0, 0) : qRgb(0, 0, 0));
    }
    MirrorState state = mirror(Qt::BitmapCursor);
    state.cursor_pixels = pixels;
    state.cursor_hotspot = QPoint(5, 5);
    state.cursor_surface_size = QSize(10, 10);
    const auto mirrored = CursorPictureOnGlass::of_mirror(state, true);
    const CursorPictureOnGlass drawn = CursorPictureOnGlass::of_guest(guest, true);
    check(mirrored.has_value() && std::abs(mirrored->scale() - 1.0) < 1e-9, "a surface of 10 units at 1.2 is 12 pixels");
    check(mirrored.has_value() && mirrored->hotspot() == QPoint(6, 6), "and its hotspot is at pixel 6");
    check(mirrored.has_value() && mirrored->aligned_with(drawn) == drawn.aligned_with(*mirrored),
          "hotspot on hotspot, the two pictures are the same");
    check(mirrored.has_value() && mirrored->drawn_around_hotspot() == drawn.drawn_around_hotspot(),
          "and drawn around the same point");
}

InputRecord raw_input(std::int32_t dx, std::int32_t dy) {
    return InputRecord{0x00FF, kGame, 0, static_cast<std::uint32_t>(dx) | (static_cast<std::uint64_t>(static_cast<std::uint32_t>(dy)) << 32), 0};
}

InputRecord mouse_move(int x, int y) {
    return InputRecord{0x0200, kGame, 0, (static_cast<std::uint64_t>(static_cast<std::uint16_t>(y)) << 16) | static_cast<std::uint16_t>(x), 0};
}

void motion_arrives_raw_and_as_the_cursor() {
    CursorTruth before = cursor(false);
    before.raw_mouse = true;
    before.recentring = kGame;
    const MotionExpectation expectation{QPoint(40, -24), game(), before};
    const std::vector<InputRecord> given{raw_input(10, -6), mouse_move(192, 113), mouse_move(180, 120),
                                         raw_input(30, -18), mouse_move(216, 98)};
    check(expectation.satisfied_by(given), "the counts and a move the right way are all there is to wait for");
    const auto checks = expectation.against(given);
    check(checks.size() == 2 && checks[0].passed && checks[1].passed, "both pass");
    check(checks.size() == 2 && checks[1].actual.contains(QStringLiteral("48,-29")),
          "moves away from the middle of a window that puts it back are added up");
    const std::vector<InputRecord> short_by_two{raw_input(10, -6), raw_input(28, -18), mouse_move(200, 110)};
    check(!expectation.satisfied_by(short_by_two) && !expectation.against(short_by_two)[0].passed,
          "two counts short is not the motion");
}

}  // namespace

int main() {
    what_is_wanted_is_read_off_windows();
    the_cursor_rows_compare_what_is_worn();
    holds_are_let_go();
    a_padded_cursor_on_the_glass_is_the_guest_s();
    motion_arrives_raw_and_as_the_cursor();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("pointer expectation: all checks passed\n");
    return EXIT_SUCCESS;
}
