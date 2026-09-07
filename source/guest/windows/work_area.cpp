#include "guest/windows/work_area.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

RECT work_area() {
    RECT area{};
    if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0) == FALSE) {
        area.right = ::GetSystemMetrics(SM_CXSCREEN);
        area.bottom = ::GetSystemMetrics(SM_CYSCREEN);
    }
    return area;
}

}  // namespace

std::uint32_t WorkArea::width() const {
    const RECT area = work_area();
    return static_cast<std::uint32_t>(area.right - area.left);
}

std::uint32_t WorkArea::height() const {
    const RECT area = work_area();
    return static_cast<std::uint32_t>(area.bottom - area.top);
}

std::int32_t WorkArea::left() const { return work_area().left; }

std::int32_t WorkArea::top() const { return work_area().top; }

}  // namespace rwin::guest
