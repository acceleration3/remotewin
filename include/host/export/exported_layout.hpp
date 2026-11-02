#pragma once

#include <cstdint>

namespace rwin::host {
struct ExportedLayout {
    static constexpr std::uint32_t kArgb8888 = 0x34325241;
    static constexpr std::uint64_t kLinearModifier = 0;
    static constexpr std::uint64_t kImplicitModifier = 0x00FF'FFFF'FFFF'FFFFULL;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fourcc = 0;
    std::uint32_t stride = 0;
    std::uint32_t offset = 0;
    std::uint64_t modifier = 0;

    [[nodiscard]] bool operator==(const ExportedLayout&) const noexcept = default;
};

}
