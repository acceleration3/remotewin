#pragma once

#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/size.hpp"
#include "proto/min_max.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rwin::host {

// One guest window as the guest last described it: its fields as they
// stand after every change it was sent (WindowNew, WindowChanged), and
// what Windows last answered for its size limits (MinMax).
//
// The accessors read the fields the way the host uses them. The picture -
// the part of the window its frames show - is what a mirror is the size
// of and where input is measured from, so positions and the region are
// given against it rather than against the frame. Everything is in guest
// pixels.
class GuestWindow {
public:
    // The invisible resize borders outside the frame.
    struct Margins {
        int left = 0;
        int top = 0;
        int right = 0;
        int bottom = 0;

        [[nodiscard]] bool operator==(const Margins&) const noexcept = default;
    };

    GuestWindow(WindowId id, WindowFields fields) : id_(id), fields_(std::move(fields)) {}

    [[nodiscard]] WindowId id() const noexcept { return id_; }
    [[nodiscard]] const WindowFields& fields() const noexcept { return fields_; }
    void take(WindowFields fields) { fields_ = std::move(fields); }

    // The window that owns this one; none when nothing does, or when its
    // owner is hidden - a window of its own then, as Windows' taskbar has it.
    [[nodiscard]] WindowId owner() const noexcept;
    // The window it opens against (POPUP); none when there is no such.
    [[nodiscard]] WindowId anchor() const noexcept { return fields_.popup.anchor; }

    [[nodiscard]] bool hidden() const noexcept { return fields_.show == WindowFields::Show::Hidden; }
    [[nodiscard]] bool minimized() const noexcept { return fields_.show == WindowFields::Show::Minimized; }
    [[nodiscard]] bool maximized() const noexcept { return fields_.show == WindowFields::Show::Maximized; }
    // Something a mirror can show: not hidden, and of some size. WinUI shows
    // a popup at 0x0 and gives it its size a frame later.
    [[nodiscard]] bool showable() const noexcept { return !hidden() && !picture_size().empty(); }

    [[nodiscard]] const std::string& title() const noexcept { return fields_.title; }

    // The DWM frame on the guest's screen.
    [[nodiscard]] Box frame() const noexcept;
    // Where the picture's corner is on the guest's screen.
    [[nodiscard]] Point picture_origin() const noexcept;
    // Where the picture lies in the frame.
    [[nodiscard]] Point picture_offset() const noexcept;
    [[nodiscard]] Size picture_size() const noexcept;
    // How much larger the frame is than the picture: what a picture of a
    // size asked for needs added to be the frame the guest is sent.
    [[nodiscard]] Size picture_padding() const noexcept;
    [[nodiscard]] Margins margins() const noexcept;
    // The window's own shape in the picture's pixels; empty when it is its
    // frame.
    [[nodiscard]] std::vector<Box> region() const;

    [[nodiscard]] const std::optional<MinMax>& limits() const noexcept { return limits_; }
    void limit(const MinMax& limits) noexcept { limits_ = limits; }

private:
    WindowId id_;
    WindowFields fields_;
    std::optional<MinMax> limits_;
};

}  // namespace rwin::host
