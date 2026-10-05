#include "host/cursor/native_cursor_shape.hpp"

namespace rwin::host {

// By what each is for, as the CSS cursor names (which wp_cursor_shape_v1
// takes) describe it: IDC_APPSTARTING is an arrow with a busy sign
// ("progress", Qt::BusyCursor), IDC_WAIT the busy sign alone ("wait"),
// IDC_SIZEALL a move in any direction ("all-scroll", Qt::SizeAllCursor).
std::optional<Qt::CursorShape> native_shape_for(SystemCursor cursor) noexcept {
    switch (cursor) {
        case SystemCursor::Arrow:
            return Qt::ArrowCursor;
        case SystemCursor::IBeam:
            return Qt::IBeamCursor;
        case SystemCursor::Wait:
            return Qt::WaitCursor;
        case SystemCursor::Cross:
            return Qt::CrossCursor;
        case SystemCursor::UpArrow:
            return Qt::UpArrowCursor;
        case SystemCursor::SizeNwse:
            return Qt::SizeFDiagCursor;
        case SystemCursor::SizeNesw:
            return Qt::SizeBDiagCursor;
        case SystemCursor::SizeWe:
            return Qt::SizeHorCursor;
        case SystemCursor::SizeNs:
            return Qt::SizeVerCursor;
        case SystemCursor::SizeAll:
            return Qt::SizeAllCursor;
        case SystemCursor::No:
            return Qt::ForbiddenCursor;
        case SystemCursor::Hand:
            return Qt::PointingHandCursor;
        case SystemCursor::AppStarting:
            return Qt::BusyCursor;
        case SystemCursor::Help:
            return Qt::WhatsThisCursor;
        case SystemCursor::None:
        case SystemCursor::Pin:
        case SystemCursor::Person:
            break;
    }
    return std::nullopt;
}

}  // namespace rwin::host
