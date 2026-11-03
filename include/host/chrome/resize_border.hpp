#pragma once

#include "host/cursor/cursor_shape.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
#include "proto/move_size_kind.hpp"

#include <cstdint>
#include <optional>

namespace rwin::host {
class ResizeBorder {
public:
    static constexpr std::uint32_t kTop = 1;
    static constexpr std::uint32_t kBottom = 2;
    static constexpr std::uint32_t kLeft = 4;
    static constexpr std::uint32_t kRight = 8;

    void fit_to(int left, int top, int right, int bottom) noexcept;

    [[nodiscard]] std::uint32_t at(PointF position, Size size) const noexcept;

    [[nodiscard]] static CursorShape cursor_for(std::uint32_t edges) noexcept;
    [[nodiscard]] static std::optional<std::uint32_t> edges_of(MoveSizeKind kind) noexcept;

private:
    static constexpr int kThickness = 6;
    static constexpr int kCornerReach = 16;

    int left_ = kThickness;
    int top_ = kThickness;
    int right_ = kThickness;
    int bottom_ = kThickness;
};

}
