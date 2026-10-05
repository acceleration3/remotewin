#pragma once

#include "proto/cursor_picture.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <utility>

namespace rwin::guest {

// Turns a guest cursor handle into what the host is sent of it: which
// system cursor it is, if it is one, and every picture of it with how long
// each is shown.
//
// A system cursor is known by its handle: LoadCursor's are shared, one
// handle per IDC_ name for every process, and that is the handle
// GetCursorInfo reports while one is up. An animated cursor's steps are read
// through GetCursorFrameInfo, which user32 exports without declaring - the
// API that draws them (DrawIconEx's step) says neither how many there are
// nor how long each lasts.
class CursorDecoder {
public:
    CursorDecoder();

    // Nothing when not even its first picture can be read.
    [[nodiscard]] std::optional<CursorPicture> decode(void* cursor_handle) const;

private:
    // HCURSOR GetCursorFrameInfo(HCURSOR, DWORD reserved, DWORD step,
    // DWORD* rate_in_jiffies, DWORD* steps).
    using FrameInfo = void* (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t*, std::uint32_t*);

    [[nodiscard]] SystemCursor system_cursor_of(void* cursor_handle) const noexcept;

    static constexpr std::size_t kSystemCursors = 16;
    std::array<std::pair<void*, SystemCursor>, kSystemCursors> system_{};
    FrameInfo frame_info_ = nullptr;
};

}  // namespace rwin::guest
