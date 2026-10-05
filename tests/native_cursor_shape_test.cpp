#include "host/cursor/native_cursor_shape.hpp"
#include "proto/system_cursor.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>

namespace {

using rwin::SystemCursor;
using rwin::host::native_shape_for;

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

// The wp_cursor_shape_device_v1 shape Qt 6.11 asks for a Qt shape by
// (qwaylandcursor.cpp, qtCursorShapeToWaylandShape) - which is the name the
// compositor draws, and so what the guest's cursor turns into on the glass.
const char* css_name_of(Qt::CursorShape shape) {
    switch (shape) {
        case Qt::ArrowCursor:
            return "default";
        case Qt::IBeamCursor:
            return "text";
        case Qt::WaitCursor:
            return "wait";
        case Qt::CrossCursor:
            return "crosshair";
        case Qt::UpArrowCursor:
            return "n-resize";
        case Qt::SizeFDiagCursor:
            return "nwse-resize";
        case Qt::SizeBDiagCursor:
            return "nesw-resize";
        case Qt::SizeHorCursor:
            return "ew-resize";
        case Qt::SizeVerCursor:
            return "ns-resize";
        case Qt::SizeAllCursor:
            return "all-scroll";
        case Qt::ForbiddenCursor:
            return "not-allowed";
        case Qt::PointingHandCursor:
            return "pointer";
        case Qt::BusyCursor:
            return "progress";
        case Qt::WhatsThisCursor:
            return "help";
        default:
            return "unmapped";
    }
}

struct Meaning {
    SystemCursor cursor;
    const char* css;
};

// What each IDC_ cursor means, as a CSS cursor name.
constexpr std::array kMeanings{
    Meaning{SystemCursor::Arrow, "default"},         Meaning{SystemCursor::IBeam, "text"},
    Meaning{SystemCursor::Wait, "wait"},             Meaning{SystemCursor::Cross, "crosshair"},
    Meaning{SystemCursor::UpArrow, "n-resize"},      Meaning{SystemCursor::SizeNwse, "nwse-resize"},
    Meaning{SystemCursor::SizeNesw, "nesw-resize"},  Meaning{SystemCursor::SizeWe, "ew-resize"},
    Meaning{SystemCursor::SizeNs, "ns-resize"},      Meaning{SystemCursor::SizeAll, "all-scroll"},
    Meaning{SystemCursor::No, "not-allowed"},        Meaning{SystemCursor::Hand, "pointer"},
    Meaning{SystemCursor::AppStarting, "progress"},  Meaning{SystemCursor::Help, "help"},
};

void every_system_cursor_is_worn_as_its_meaning() {
    for (const Meaning& meaning : kMeanings) {
        const std::optional<Qt::CursorShape> shape = native_shape_for(meaning.cursor);
        if (!shape.has_value() || std::strcmp(css_name_of(*shape), meaning.css) != 0) {
            std::printf("  %s: wanted %s, got %s\n", rwin::name_of(meaning.cursor), meaning.css,
                        shape.has_value() ? css_name_of(*shape) : "nothing");
            check(false, "a system cursor is this desktop's cursor of the same meaning");
        }
    }
}

void cursors_no_desktop_has_are_the_guest_s_pixels() {
    check(!native_shape_for(SystemCursor::Pin).has_value(), "IDC_PIN has no desktop cursor");
    check(!native_shape_for(SystemCursor::Person).has_value(), "IDC_PERSON has no desktop cursor");
    check(!native_shape_for(SystemCursor::None).has_value(), "an application's own cursor is its pixels");
}

void no_system_cursor_is_left_out() {
    std::size_t named = 0;
    for (auto cursor = static_cast<int>(SystemCursor::Arrow); cursor <= static_cast<int>(SystemCursor::Person);
         ++cursor) {
        if (native_shape_for(static_cast<SystemCursor>(cursor)).has_value()) ++named;
    }
    check(named == kMeanings.size(), "every system cursor but IDC_PIN and IDC_PERSON has a meaning here");
}

}  // namespace

int main() {
    every_system_cursor_is_worn_as_its_meaning();
    cursors_no_desktop_has_are_the_guest_s_pixels();
    no_system_cursor_is_left_out();
    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("ok\n");
    return EXIT_SUCCESS;
}
