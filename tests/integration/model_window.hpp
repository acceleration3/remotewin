#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_fields.hpp"

#include <cstdint>
#include <optional>

namespace rwin::integration {

// One top-level window as Windows keeps it, for a guest the harness plays:
// an overlapped window with a caption, a sizing border, and minimise and
// maximise boxes. What it is asked (a WindowMove, a SysCommand) it does as
// Windows does - window_commands.cpp's place() for a move, the system
// commands' own rectangles for the rest - and it says what it now is as
// the fields the real guest reads.
class ModelWindow {
public:
    struct Shape {
        // The DWM frame, on the guest's screen.
        Rect frame;
        // Where a maximised window goes, and what a move or size is kept in.
        Rect work_area;
        // The smallest frame Windows lets it be (WM_GETMINMAXINFO's track size
        // less the invisible borders).
        std::uint32_t min_width = 0;
        std::uint32_t min_height = 0;
        // The invisible resize borders outside the frame; none at the top, as
        // on Windows 10 and 11, where the top edge sizes from inside.
        std::uint16_t margin_side = 0;
        std::uint16_t margin_bottom = 0;
        // How deep the top edge's sizing strip is, inside the frame.
        std::int32_t top_strip = 0;
        std::int32_t caption_height = 0;
        std::string title;
    };

    explicit ModelWindow(Shape shape);

    [[nodiscard]] const Rect& frame() const noexcept { return frame_; }
    [[nodiscard]] WindowFields::Show show() const noexcept { return show_; }
    [[nodiscard]] bool maximized() const noexcept { return show_ == WindowFields::Show::Maximized; }
    [[nodiscard]] bool minimized() const noexcept { return show_ == WindowFields::Show::Minimized; }
    // Where a restore puts it back (WINDOWPLACEMENT's normal position).
    [[nodiscard]] const Rect& normal() const noexcept { return normal_; }

    // window_commands.cpp place(): nothing while minimised or maximised; a
    // new size is kept within the work area, a move alone is not.
    void place(const Rect& frame);
    void command(SysCommand::Command command);
    // What Windows' hit test makes of a press at a point of the frame: the
    // kind of move or size loop it begins, if any.
    [[nodiscard]] std::optional<MoveSizeKind> loop_at(std::int32_t x, std::int32_t y) const;

    [[nodiscard]] WindowFields fields() const;
    [[nodiscard]] const Rect& work_area() const noexcept { return shape_.work_area; }

private:
    Shape shape_;
    Rect frame_;
    Rect normal_;
    WindowFields::Show show_ = WindowFields::Show::Shown;
    // Maximised before it was minimised: a restore maximises it again.
    bool was_maximized_ = false;
};

}  // namespace rwin::integration
