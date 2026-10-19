#pragma once

#include "host/cursor/cursor_shape.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
#include "proto/move_size_kind.hpp"

#include <cstdint>
#include <optional>

namespace rwin::host {

// The band around a mirror's edge that starts a resize.
//
// A window's resize border sits OUTSIDE its visible frame - that is
// exactly what the invisible border is - so no point inside the mirror is
// ever in it, and the mirror has no chrome of this desktop's to grab
// either. So the band is inside the mirror's own edges, as wide as the
// guest says the window's borders are, corners winning over sides.
//
// Edges are xdg_toplevel's resize_edge bits, which a corner is two of.
class ResizeBorder {
public:
    static constexpr std::uint32_t kTop = 1;
    static constexpr std::uint32_t kBottom = 2;
    static constexpr std::uint32_t kLeft = 4;
    static constexpr std::uint32_t kRight = 8;

    // The window's borders, in the mirror's logical units. None at all for
    // a window with a sizing border is a guest that did not say, and the
    // band is the host's own width.
    void fit_to(int left, int top, int right, int bottom) noexcept;

    // The edges a point of a mirror of ``size`` is on; 0 for none.
    [[nodiscard]] std::uint32_t at(PointF position, Size size) const noexcept;

    // The cursor that says those edges can be dragged.
    [[nodiscard]] static CursorShape cursor_for(std::uint32_t edges) noexcept;
    // The edges of a resize Windows began; nothing for a move.
    [[nodiscard]] static std::optional<std::uint32_t> edges_of(MoveSizeKind kind) noexcept;

private:
    // Wide enough to hit without care, narrow enough not to swallow a
    // click meant for a scrollbar sitting against the same edge.
    static constexpr int kThickness = 6;

    int left_ = kThickness;
    int top_ = kThickness;
    int right_ = kThickness;
    int bottom_ = kThickness;
};

}  // namespace rwin::host
