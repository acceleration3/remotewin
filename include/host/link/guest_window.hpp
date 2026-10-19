#pragma once

#include "proto/min_max.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <QList>
#include <QMargins>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>
#include <utility>

namespace rwin::host {

// One guest window as the guest last described it: its fields as they
// stand after every change it was sent (WindowNew, WindowChanged), and
// what Windows last answered for its size limits (MinMax).
//
// The accessors read the fields the way the host uses them. The picture -
// the part of the window its frames show - is what a mirror is the size
// of and where input is measured from, so positions and the region are
// given against it rather than against the frame.
class GuestWindow {
public:
    GuestWindow(WindowId id, WindowFields fields) : id_(id), fields_(std::move(fields)) {}

    [[nodiscard]] WindowId id() const noexcept { return id_; }
    [[nodiscard]] const WindowFields& fields() const noexcept { return fields_; }
    void take(WindowFields fields) { fields_ = std::move(fields); }

    // The window that owns this one; none when nothing does, or when its
    // owner is hidden - a window of its own then, as Windows' taskbar has it.
    [[nodiscard]] WindowId owner() const noexcept;
    // The window it is placed against without being owned by it; none
    // when there is no such window.
    [[nodiscard]] WindowId anchor() const noexcept { return fields_.popup.anchor; }

    [[nodiscard]] bool hidden() const noexcept { return fields_.show == WindowFields::Show::Hidden; }
    [[nodiscard]] bool minimized() const noexcept { return fields_.show == WindowFields::Show::Minimized; }
    [[nodiscard]] bool maximized() const noexcept { return fields_.show == WindowFields::Show::Maximized; }

    [[nodiscard]] QString title() const { return QString::fromStdString(fields_.title); }
    [[nodiscard]] QString class_name() const { return QString::fromStdString(fields_.class_name); }

    // The DWM frame on the guest's screen.
    [[nodiscard]] QRect frame() const;
    // Where the picture's corner is on the guest's screen.
    [[nodiscard]] QPoint picture_origin() const;
    [[nodiscard]] QSize picture_size() const;
    // How much larger the frame is than the picture: what a picture of a
    // size asked for needs added to be the frame the guest is sent.
    [[nodiscard]] QSize picture_padding() const;
    // The invisible resize borders outside the frame, in guest pixels.
    [[nodiscard]] QMargins margins() const;
    // The window's own shape in the picture's pixels; empty when it is its
    // frame.
    [[nodiscard]] QList<QRect> region() const;
    // Where the picture sits against the anchor's picture, for a window
    // placed against one (anchor()).
    [[nodiscard]] QPoint offset_from_anchor(const GuestWindow& anchor) const;

    [[nodiscard]] const std::optional<MinMax>& limits() const noexcept { return limits_; }
    void limit(const MinMax& limits) noexcept { limits_ = limits; }

private:
    WindowId id_;
    WindowFields fields_;
    std::optional<MinMax> limits_;
};

}  // namespace rwin::host
