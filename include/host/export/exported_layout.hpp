#pragma once

#include <cstdint>

namespace rwin::host {

// How an exported surface's picture lies in its dma-buf: what EGL is told
// when it imports it.
struct ExportedLayout {
    // DRM_FORMAT_ARGB8888 ("AR24"): B8G8R8A8 in memory, what the guest's
    // rings are made of.
    static constexpr std::uint32_t kArgb8888 = 0x34325241;
    // DRM_FORMAT_MOD_INVALID: the layout the driver keeps with the memory,
    // readable only on the same GPU.
    static constexpr std::uint64_t kImplicitModifier = 0x00FF'FFFF'FFFF'FFFFULL;

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t fourcc = 0;
    // Bytes from one row to the next, from ``offset`` bytes in.
    std::uint32_t stride = 0;
    std::uint32_t offset = 0;
    std::uint64_t modifier = 0;

    [[nodiscard]] bool operator==(const ExportedLayout&) const noexcept = default;
};

}  // namespace rwin::host
