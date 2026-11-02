#pragma once

#include "proto/cursor_picture.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace rwin::guest {
class CursorDecoder {
public:
    CursorDecoder();

    [[nodiscard]] std::optional<CursorPicture> decode(void* cursor_handle) const;

private:
    using FrameInfo = void* (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t*, std::uint32_t*);

    [[nodiscard]] SystemCursor system_cursor_of(void* cursor_handle) const noexcept;

    static constexpr std::size_t kSystemCursors = 16;
    std::array<std::pair<void*, SystemCursor>, kSystemCursors> system_{};
    FrameInfo frame_info_ = nullptr;
};

}
