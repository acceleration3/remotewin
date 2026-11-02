#pragma once

#include <cstdint>

namespace rwin {
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

[[nodiscard]] std::uint32_t idc_of(SystemCursor cursor) noexcept;

[[nodiscard]] SystemCursor system_cursor_of(std::uint32_t idc) noexcept;

[[nodiscard]] const char* name_of(SystemCursor cursor) noexcept;

}
