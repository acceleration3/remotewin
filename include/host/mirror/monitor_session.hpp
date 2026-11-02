#pragma once

#include "host/cursor/cursor_player.hpp"
#include "host/geometry/size.hpp"
#include "host/input/held_keys.hpp"
#include "host/loop/signal.hpp"
#include "host/wayland/backdrop.hpp"
#include "host/wayland/mirror_surface.hpp"
#include "host/wayland/surface_input.hpp"
#include "host/wayland/surface_kit.hpp"
#include "host/wayland/toplevel_configure.hpp"
#include "host/wayland/toplevel_events.hpp"
#include "host/wayland/toplevel_role.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rwin::host {

class Activation;
class GuestLink;
class SharedFrameFeed;
struct CursorLook;

class MonitorSession final : public SurfaceInput, public ToplevelEvents {
public:
    MonitorSession(const SurfaceKit& kit, GuestLink& link, SharedFrameFeed& frames, Activation& activation,
                   const std::string& title, const std::string& app_id, std::string token);
    ~MonitorSession();

    MonitorSession(const MonitorSession&) = delete;
    MonitorSession& operator=(const MonitorSession&) = delete;
    MonitorSession(MonitorSession&&) = delete;
    MonitorSession& operator=(MonitorSession&&) = delete;

    void raise(const std::string& token);

    Signal<> finished;
    Signal<> ready;

private:
    void pointer_entered(PointF at) override;
    void pointer_moved(PointF at) override;
    void pointer_left() override;
    void button(std::uint32_t code, bool pressed) override;
    void wheel(Wheel turned) override;
    void keyboard_entered() override;
    void keyboard_left() override;
    void key(const KeyPress& press) override;
    void configured(const ToplevelConfigure& configure) override;
    void close_requested() override;

    void show_pixels(const PixelsView& pixels);
    void patch(Size picture, Point at, const PixelsView& pixels);
    void fit();
    void wear_cursor();
    void note_ready();
    [[nodiscard]] std::optional<Point> picture_point(PointF at) const;

    SurfaceKit kit_;
    GuestLink& link_;
    SharedFrameFeed& frames_;
    Activation& activation_;
    CursorPlayer cursor_;
    MirrorSurface surface_;
    ToplevelRole toplevel_;
    std::unique_ptr<Backdrop> backdrop_;
    ToplevelConfigure configure_;
    HeldKeys held_keys_;
    std::vector<std::uint8_t> canvas_;
    Size canvas_size_;
    std::optional<PointF> pointer_;
    std::string token_;
    bool ready_ = false;
};

}
