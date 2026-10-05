#pragma once

#include <cstdint>

namespace rwin {

// The cursors Windows ships (LoadCursor with an IDC_ name), by what they
// mean. A guest cursor that is one of them is shown as this desktop's own
// cursor of the same meaning - drawn by the compositor, sharp at any scale,
// in the user's theme - rather than as the guest's pixels.
enum class SystemCursor : std::uint8_t {
    None,
    Arrow,
    IBeam,
    Wait,
    Cross,
    UpArrow,
    SizeNwse,
    SizeNesw,
    SizeWe,
    SizeNs,
    SizeAll,
    No,
    Hand,
    AppStarting,
    Help,
    Pin,
    Person,
};

// The IDC_ number LoadCursor takes for it (IDC_ARROW is 32512), 0 for None.
[[nodiscard]] std::uint32_t idc_of(SystemCursor cursor) noexcept;

// None for a number that names no cursor Windows ships.
[[nodiscard]] SystemCursor system_cursor_of(std::uint32_t idc) noexcept;

// What winuser.h calls it, for a reader.
[[nodiscard]] const char* name_of(SystemCursor cursor) noexcept;

}  // namespace rwin
