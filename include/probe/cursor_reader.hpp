#pragma once

#include "proto/probe/cursor_truth.hpp"

#include <array>
#include <cstdint>
#include <utility>

struct HICON__;

namespace rwin::probe {

class PointerActions;

// Reads what Windows says about the cursor and the pointer now
// (CursorTruth): GetCursorInfo, the clip, the capture, the raw input
// registration, the ballistics, and the cursor's every picture as the user
// sees it - DrawIconEx over white and over black, step by step.
class CursorReader {
public:
    explicit CursorReader(const PointerActions& pointer);

    [[nodiscard]] CursorTruth read() const;

private:
    // GetCursorFrameInfo, which user32 exports without declaring:
    // HCURSOR (HCURSOR, DWORD reserved, DWORD step, DWORD* jiffies, DWORD* steps).
    using FrameInfo = void* (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t*, std::uint32_t*);

    [[nodiscard]] std::uint32_t system_idc_of(HICON__* cursor) const noexcept;
    void read_frames(HICON__* cursor, CursorTruth& truth) const;
    static void read_pointer_settings(CursorTruth& truth);

    static constexpr std::size_t kSystemCursors = 16;
    const PointerActions& pointer_;
    std::array<std::pair<HICON__*, std::uint32_t>, kSystemCursors> system_{};
    FrameInfo frame_info_ = nullptr;
};

}  // namespace rwin::probe
