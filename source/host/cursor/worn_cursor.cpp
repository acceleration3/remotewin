#include "host/cursor/worn_cursor.hpp"

#include "host/cursor/cursor_look.hpp"
#include "host/cursor/native_cursor_shape.hpp"
#include "host/cursor/pixel_cursor.hpp"

#include <QWidget>

#include <optional>

namespace rwin::host {

void WornCursor::wear(const CursorLook* look, std::size_t step) {
    hidden_ = look == nullptr;
    look_ = look;
    step_ = look == nullptr || look->frames.empty() ? 0 : step % look->frames.size();
}

void WornCursor::put_on(QWidget& widget) {
    if (hidden_) {
        widget.setCursor(Qt::BlankCursor);
        return;
    }
    if (look_ == nullptr || look_->frames.empty()) {
        widget.unsetCursor();
        return;
    }
    if (const std::optional<Qt::CursorShape> native = native_shape_for(look_->system)) {
        widget.setCursor(*native);
        return;
    }
    pixels_.put_on(widget, look_->frames[step_]);
}

}  // namespace rwin::host
