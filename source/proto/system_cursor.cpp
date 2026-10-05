#include "proto/system_cursor.hpp"

#include <array>

namespace rwin {

namespace {

struct Entry {
    SystemCursor cursor;
    std::uint32_t idc;
    const char* name;
};

constexpr std::array kCursors{
    Entry{SystemCursor::Arrow, 32512, "IDC_ARROW"},
    Entry{SystemCursor::IBeam, 32513, "IDC_IBEAM"},
    Entry{SystemCursor::Wait, 32514, "IDC_WAIT"},
    Entry{SystemCursor::Cross, 32515, "IDC_CROSS"},
    Entry{SystemCursor::UpArrow, 32516, "IDC_UPARROW"},
    Entry{SystemCursor::SizeNwse, 32642, "IDC_SIZENWSE"},
    Entry{SystemCursor::SizeNesw, 32643, "IDC_SIZENESW"},
    Entry{SystemCursor::SizeWe, 32644, "IDC_SIZEWE"},
    Entry{SystemCursor::SizeNs, 32645, "IDC_SIZENS"},
    Entry{SystemCursor::SizeAll, 32646, "IDC_SIZEALL"},
    Entry{SystemCursor::No, 32648, "IDC_NO"},
    Entry{SystemCursor::Hand, 32649, "IDC_HAND"},
    Entry{SystemCursor::AppStarting, 32650, "IDC_APPSTARTING"},
    Entry{SystemCursor::Help, 32651, "IDC_HELP"},
    Entry{SystemCursor::Pin, 32671, "IDC_PIN"},
    Entry{SystemCursor::Person, 32672, "IDC_PERSON"},
};

}  // namespace

std::uint32_t idc_of(SystemCursor cursor) noexcept {
    for (const Entry& entry : kCursors) {
        if (entry.cursor == cursor) return entry.idc;
    }
    return 0;
}

SystemCursor system_cursor_of(std::uint32_t idc) noexcept {
    for (const Entry& entry : kCursors) {
        if (entry.idc == idc) return entry.cursor;
    }
    return SystemCursor::None;
}

const char* name_of(SystemCursor cursor) noexcept {
    for (const Entry& entry : kCursors) {
        if (entry.cursor == cursor) return entry.name;
    }
    return "none";
}

}  // namespace rwin
