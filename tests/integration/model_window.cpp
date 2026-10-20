#include "model_window.hpp"

#include <algorithm>

namespace rwin::integration {

namespace {

// Where Windows parks a minimised window, and the size it gives it there.
constexpr Rect kMinimizedFrame{-32000, -32000, 160, 28};
// WS_OVERLAPPEDWINDOW: caption, system menu, sizing border, both boxes.
constexpr std::uint32_t kOverlappedWindow = 0x00CF0000;
constexpr std::uint32_t kAppWindow = 0x00040000;

std::int32_t kept_in(std::int32_t at, std::int32_t room_from, std::uint32_t room_size, std::uint32_t size) {
    return std::clamp(at, room_from, room_from + static_cast<std::int32_t>(room_size - std::min(size, room_size)));
}

}  // namespace

ModelWindow::ModelWindow(Shape shape) : shape_(std::move(shape)), frame_(shape_.frame), normal_(shape_.frame) {}

void ModelWindow::place(const Rect& frame) {
    if (minimized() || maximized()) return;
    const bool resized = frame.width != frame_.width || frame.height != frame_.height;
    if (!resized) {
        frame_.x = frame.x;
        frame_.y = frame.y;
        normal_ = frame_;
        return;
    }
    const Rect& room = shape_.work_area;
    const std::uint32_t width = std::max(shape_.min_width, std::min(frame.width, room.width));
    const std::uint32_t height = std::max(shape_.min_height, std::min(frame.height, room.height));
    frame_ = Rect{kept_in(frame.x, room.x, room.width, width), kept_in(frame.y, room.y, room.height, height), width,
                  height};
    normal_ = frame_;
}

void ModelWindow::command(SysCommand::Command command) {
    switch (command) {
        case SysCommand::Command::Maximize:
            if (maximized()) return;
            if (!minimized()) normal_ = frame_;
            frame_ = shape_.work_area;
            show_ = WindowFields::Show::Maximized;
            return;
        case SysCommand::Command::Minimize:
            if (minimized()) return;
            was_maximized_ = maximized();
            frame_ = kMinimizedFrame;
            show_ = WindowFields::Show::Minimized;
            return;
        case SysCommand::Command::Restore:
            if (minimized() && was_maximized_) {
                frame_ = shape_.work_area;
                show_ = WindowFields::Show::Maximized;
                return;
            }
            frame_ = normal_;
            show_ = WindowFields::Show::Shown;
            return;
        default:
            return;
    }
}

std::optional<MoveSizeKind> ModelWindow::loop_at(std::int32_t x, std::int32_t y) const {
    if (minimized() || maximized()) return std::nullopt;
    const auto width = static_cast<std::int32_t>(frame_.width);
    if (y < shape_.top_strip) {
        if (x < shape_.top_strip) return MoveSizeKind::TopLeft;
        if (x >= width - shape_.top_strip) return MoveSizeKind::TopRight;
        return MoveSizeKind::Top;
    }
    if (y < shape_.caption_height) return MoveSizeKind::Move;
    return std::nullopt;
}

WindowFields ModelWindow::fields() const {
    WindowFields fields;
    fields.style.style = kOverlappedWindow;
    fields.style.extended = kAppWindow;
    fields.show = show_;
    fields.title = shape_.title;
    fields.geometry.frame = frame_;
    fields.geometry.client = Rect{0, shape_.caption_height, frame_.width,
                                  frame_.height - std::min<std::uint32_t>(frame_.height, shape_.caption_height)};
    if (!maximized() && !minimized()) {
        fields.geometry.margin_left = shape_.margin_side;
        fields.geometry.margin_right = shape_.margin_side;
        fields.geometry.margin_bottom = shape_.margin_bottom;
    }
    fields.geometry.picture = Rect{0, 0, frame_.width, frame_.height};
    fields.taskbar = WindowFields::kTaskbarButton | WindowFields::kInSwitchers;
    fields.frame = WindowFields::kDwmFrame | WindowFields::kDropShadow;
    fields.class_name = "RemotewinIntegrationWindow";
    fields.app_id.process_id = 4242;
    fields.app_id.image = "C:\\Program Files\\Integration\\model.exe";
    fields.dpi = 96;
    return fields;
}

}  // namespace rwin::integration
