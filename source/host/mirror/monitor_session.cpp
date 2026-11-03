#include "host/mirror/monitor_session.hpp"

#include "host/export/shared_frame.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/geometry/view_mapping.hpp"
#include "host/input/guest_button.hpp"
#include "host/input/key_translator.hpp"
#include "host/input/qmp_input.hpp"
#include "host/link/guest_link.hpp"
#include "host/wayland/activation.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/key_press.hpp"
#include "host/cursor/native_cursor_shape.hpp"
#include "host/wayland/seat.hpp"
#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/feature.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/pointer_wheel.hpp"
#include "proto/window_id.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rwin::host {

MonitorSession::MonitorSession(const SurfaceKit& kit, GuestLink& link, SharedFrameFeed& frames,
                               Activation& activation, const std::string& title, const std::string& app_id,
                               std::string token, QmpInput* qmp_input)
    : kit_(kit), link_(link), frames_(frames), activation_(activation), qmp_input_(qmp_input), cursor_(kit.loop), surface_(kit, *this),
      toplevel_(kit.globals, surface_, *this, true), token_(std::move(token)) {
    toplevel_.set_title(title);
    if (!app_id.empty()) toplevel_.set_app_id(app_id);
    toplevel_.start();
    link_.connected.connect([this] {
        if (!link_.features().has(Feature::kMonitor)) {
            RWIN_ERROR("monitor", "the guest half cannot show its monitor; it does not speak {}", Feature::kMonitor);
            finished();
            return;
        }
        link_.watch_monitor();
    });
    link_.frame_arrived.connect([this](WindowId window, const PixelsView& pixels) {
        if (window == kMonitorWindow) show_pixels(pixels);
    });
    link_.frame_patched.connect([this](WindowId window, Size picture, Point at, const PixelsView& pixels) {
        if (window == kMonitorWindow) patch(picture, at, pixels);
    });
    link_.frame_patches_done.connect([this](WindowId window) {
        if (window != kMonitorWindow || canvas_.empty()) return;
        surface_.show(PixelsView{canvas_.data(), canvas_size_.width, canvas_size_.height, canvas_size_.width * 4});
        note_ready();
    });
    frames_.frame_ready.connect([this](WindowId window, const SharedFrame& frame) {
        if (window != kMonitorWindow) return;
        surface_.show(frame);
        note_ready();
    });
    link_.surface_lost.connect([this](WindowId window) {
        if (window != kMonitorWindow) return;
        RWIN_ERROR("monitor", "the guest can no longer capture its monitor");
        finished();
    });
    link_.cursor_changed.connect([this](const GuestCursor& cursor) { cursor_.show(cursor); });
    link_.cursor_pictured.connect([this](const CursorPicture& picture) { cursor_.remember(picture); });
    cursor_.worn.connect([this](const CursorLook*, std::size_t) { wear_cursor(); });
    surface_.picture_resized.connect([this] { fit(); });
    surface_.shared_refused.connect([this](const std::string& why) { frames_.pixels_for(kMonitorWindow, why); });
    surface_.presented.connect([this](const FrameStamps& stamps) {
        frames_.shown(stamps, kit_.seat.keyboard_focus() == surface_.surface());
    });
    surface_.first_mapped.connect([this] {
        if (!token_.empty()) activation_.activate(surface_.surface(), std::exchange(token_, std::string()));
    });
}

MonitorSession::~MonitorSession() = default;

void MonitorSession::raise(const std::string& token) {
    if (token.empty()) {
        activation_.hand_to(surface_.surface());
    } else {
        activation_.activate(surface_.surface(), token);
    }
}

void MonitorSession::note_ready() {
    if (ready_) return;
    ready_ = true;
    ready();
}

void MonitorSession::show_pixels(const PixelsView& pixels) {
    const Size size{pixels.width, pixels.height};
    if (size != canvas_size_) {
        canvas_.resize(static_cast<std::size_t>(size.width) * size.height * 4);
        canvas_size_ = size;
    }
    for (int row = 0; row < size.height; ++row) {
        std::memcpy(canvas_.data() + static_cast<std::size_t>(row) * size.width * 4,
                    pixels.bgra + static_cast<std::size_t>(row) * pixels.stride, static_cast<std::size_t>(size.width) * 4);
    }
    surface_.show(PixelsView{canvas_.data(), size.width, size.height, size.width * 4});
    note_ready();
}

void MonitorSession::patch(Size picture, Point at, const PixelsView& pixels) {
    if (picture != canvas_size_) return;
    for (int row = 0; row < pixels.height; ++row) {
        std::memcpy(canvas_.data() + (static_cast<std::size_t>(at.y + row) * picture.width + at.x) * 4,
                    pixels.bgra + static_cast<std::size_t>(row) * pixels.stride,
                    static_cast<std::size_t>(pixels.width) * 4);
    }
}

void MonitorSession::configured(const ToplevelConfigure& configure) {
    configure_ = configure;
    fit();
}

void MonitorSession::close_requested() { finished(); }

void MonitorSession::fit() {
    if (!toplevel_.configured() || surface_.picture_size().empty()) return;
    const Size picture = surface_.picture_size();
    const Size natural = ViewMapping::one_to_one(picture, surface_.scale()).logical();
    const Size room = configure_.size.empty() ? natural : configure_.size;
    if (room == natural) {
        backdrop_.reset();
        surface_.fit_one_to_one();
        toplevel_.set_window_geometry(Box::of(Point{}, natural));
        surface_.commit();
        return;
    }
    const double scale = std::min(static_cast<double>(room.width) / picture.width,
                                  static_cast<double>(room.height) / picture.height);
    const Size fitted{std::max(1, static_cast<int>(std::floor(picture.width * scale))),
                      std::max(1, static_cast<int>(std::floor(picture.height * scale)))};
    surface_.fit_to(fitted);
    const Box whole{-(room.width - fitted.width) / 2, -(room.height - fitted.height) / 2, room.width, room.height};
    if (backdrop_ == nullptr) backdrop_ = std::make_unique<Backdrop>(kit_.globals, surface_.surface());
    backdrop_->cover(whole);
    toplevel_.set_window_geometry(whole);
    surface_.commit();
}

std::optional<Point> MonitorSession::picture_point(PointF at) const {
    const Point point = surface_.mapping().picture_point(at);
    if (!Box::of(Point{}, surface_.picture_size()).contains(point)) return std::nullopt;
    return point;
}

bool MonitorSession::through_qmp() const noexcept { return qmp_input_ != nullptr && qmp_input_->ready(); }

void MonitorSession::wear_cursor() {
    if (kit_.seat.pointer_focus() != surface_.surface()) return;
    PointerCursor& cursor = kit_.seat.cursor();
    const CursorLook* look = cursor_.look();
    if (!cursor_.shown().shown) {
        cursor.wear_nothing();
        return;
    }
    if (look == nullptr || look->frames.empty()) {
        cursor.wear(CursorShape::Default);
        return;
    }
    if (const std::optional<CursorShape> shape = native_shape_for(look->system); shape && cursor.draws_shapes()) {
        cursor.wear(*shape);
        return;
    }
    const std::size_t step = cursor_.step() % look->frames.size();
    cursor.wear(look->id, step, look->frames[step], surface_.scale());
}

void MonitorSession::pointer_entered(PointF at) {
    pointer_moved(at);
    wear_cursor();
}

void MonitorSession::pointer_moved(PointF at) {
    pointer_ = at;
    const std::optional<Point> point = picture_point(at);
    if (!point.has_value()) return;
    if (through_qmp()) {
        const Size picture = surface_.picture_size();
        qmp_input_->move_to(point->x, point->y, picture.width, picture.height);
        return;
    }
    link_.send_pointer_move(kMonitorWindow, *point);
}

void MonitorSession::pointer_left() { pointer_.reset(); }

void MonitorSession::button(std::uint32_t code, bool pressed) {
    if (through_qmp() && qmp_input_->button(code, pressed)) return;
    const std::optional<std::uint8_t> guest = guest_button(code);
    if (!guest.has_value() || !pointer_.has_value()) return;
    if (const std::optional<Point> point = picture_point(*pointer_)) {
        link_.send_pointer_button(kMonitorWindow, *point, *guest, pressed);
    }
}

void MonitorSession::wheel(Wheel turned) {
    if (!pointer_.has_value()) return;
    if (through_qmp()) {
        qmp_input_->wheel(turned.dx120, turned.dy120);
        return;
    }
    if (const std::optional<Point> point = picture_point(*pointer_)) {
        link_.send_pointer_wheel(PointerWheel{kMonitorWindow, point->x, point->y, turned.dx120, -turned.dy120});
    }
}

void MonitorSession::keyboard_entered() {}

void MonitorSession::keyboard_left() {
    if (qmp_input_ != nullptr) qmp_input_->release_keys();
    held_keys_.release_all(link_, kMonitorWindow);
}

void MonitorSession::key(const KeyPress& press) {
    if (through_qmp() && qmp_input_->key(press.code, press.pressed)) return;
    const std::optional<KeyStroke> stroke = stroke_for(press, kMonitorWindow);
    if (!stroke.has_value()) return;
    held_keys_.note(*stroke);
    link_.send_key(*stroke);
}

}
