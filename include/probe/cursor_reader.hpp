#pragma once

#include "proto/probe/cursor_truth.hpp"

#include <array>
#include <cstdint>
#include <utility>

struct HICON__;

namespace rwin::probe {

class PointerActions;

class CursorReader {
public:
    explicit CursorReader(const PointerActions& pointer);

    [[nodiscard]] CursorTruth read() const;

private:
    using FrameInfo = void* (*)(void*, std::uint32_t, std::uint32_t, std::uint32_t*, std::uint32_t*);

    [[nodiscard]] std::uint32_t system_idc_of(HICON__* cursor) const noexcept;
    void read_frames(HICON__* cursor, CursorTruth& truth) const;
    static void read_pointer_settings(CursorTruth& truth);

    static constexpr std::size_t kSystemCursors = 16;
    const PointerActions& pointer_;
    std::array<std::pair<HICON__*, std::uint32_t>, kSystemCursors> system_{};
    FrameInfo frame_info_ = nullptr;
};

}
