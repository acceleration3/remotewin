#pragma once

#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_fields.hpp"

#include <cstdint>
#include <optional>

namespace rwin::integration {
class ModelWindow {
public:
    struct Shape {
        Rect frame;
        Rect work_area;
        std::uint32_t min_width = 0;
        std::uint32_t min_height = 0;
        std::uint16_t margin_side = 0;
        std::uint16_t margin_bottom = 0;
        std::int32_t top_strip = 0;
        std::int32_t caption_height = 0;
        std::string title;
    };

    explicit ModelWindow(Shape shape);

    [[nodiscard]] const Rect& frame() const noexcept { return frame_; }
    [[nodiscard]] WindowFields::Show show() const noexcept { return show_; }
    [[nodiscard]] bool maximized() const noexcept { return show_ == WindowFields::Show::Maximized; }
    [[nodiscard]] bool minimized() const noexcept { return show_ == WindowFields::Show::Minimized; }
    [[nodiscard]] const Rect& normal() const noexcept { return normal_; }

    void place(const Rect& frame);
    void command(SysCommand::Command command);
    [[nodiscard]] std::optional<MoveSizeKind> loop_at(std::int32_t x, std::int32_t y) const;

    [[nodiscard]] WindowFields fields() const;
    [[nodiscard]] const Rect& work_area() const noexcept { return shape_.work_area; }

private:
    Shape shape_;
    Rect frame_;
    Rect normal_;
    WindowFields::Show show_ = WindowFields::Show::Shown;
    bool was_maximized_ = false;
};

}
