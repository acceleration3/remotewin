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

const char* css_name_of(rwin::host::CursorShape shape) {
    using rwin::host::CursorShape;
    switch (shape) {
        case CursorShape::Default: return "default";
        case CursorShape::Help: return "help";
        case CursorShape::Pointer: return "pointer";
        case CursorShape::Progress: return "progress";
        case CursorShape::Wait: return "wait";
        case CursorShape::Crosshair: return "crosshair";
        case CursorShape::Text: return "text";
        case CursorShape::NotAllowed: return "not-allowed";
        case CursorShape::NResize: return "n-resize";
        case CursorShape::EwResize: return "ew-resize";
        case CursorShape::NsResize: return "ns-resize";
        case CursorShape::NeswResize: return "nesw-resize";
        case CursorShape::NwseResize: return "nwse-resize";
        case CursorShape::AllScroll: return "all-scroll";
        default: return "unmapped";
    }
}

void shapes_are_numbered_as_the_protocol_numbers_them() {
    using rwin::host::CursorShape;
    check(static_cast<int>(CursorShape::Default) == 1 && static_cast<int>(CursorShape::Text) == 9
              && static_cast<int>(CursorShape::NotAllowed) == 15 && static_cast<int>(CursorShape::NwseResize) == 29
              && static_cast<int>(CursorShape::AllScroll) == 32,
          "a shape's number is cursor-shape-v1's");
}

struct Meaning {
    SystemCursor cursor;
    const char* css;
};

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
        const std::optional<rwin::host::CursorShape> shape = native_shape_for(meaning.cursor);
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

}

int main() {
    shapes_are_numbered_as_the_protocol_numbers_them();
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
