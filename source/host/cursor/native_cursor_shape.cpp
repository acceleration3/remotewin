#include "host/cursor/native_cursor_shape.hpp"

namespace rwin::host {
std::optional<CursorShape> native_shape_for(SystemCursor cursor) noexcept {
    switch (cursor) {
        case SystemCursor::Arrow: return CursorShape::Default;
        case SystemCursor::IBeam: return CursorShape::Text;
        case SystemCursor::Wait: return CursorShape::Wait;
        case SystemCursor::Cross: return CursorShape::Crosshair;
        case SystemCursor::UpArrow: return CursorShape::NResize;
        case SystemCursor::SizeNwse: return CursorShape::NwseResize;
        case SystemCursor::SizeNesw: return CursorShape::NeswResize;
        case SystemCursor::SizeWe: return CursorShape::EwResize;
        case SystemCursor::SizeNs: return CursorShape::NsResize;
        case SystemCursor::SizeAll: return CursorShape::AllScroll;
        case SystemCursor::No: return CursorShape::NotAllowed;
        case SystemCursor::Hand: return CursorShape::Pointer;
        case SystemCursor::AppStarting: return CursorShape::Progress;
        case SystemCursor::Help: return CursorShape::Help;
        case SystemCursor::None:
        case SystemCursor::Pin:
        case SystemCursor::Person:
            break;
    }
    return std::nullopt;
}

}
