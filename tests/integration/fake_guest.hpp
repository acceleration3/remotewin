#pragma once

#include "model_window.hpp"

#include "host/loop/event_loop.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_id.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace rwin {
class MessageReader;
}

namespace rwin::integration {

// The guest half, played: one window (ModelWindow) on the other end of a
// wire 6 link to the host under test, as remotewin-guest.exe would be. It
// answers what the host asks of the window - a WindowMove, a SysCommand -
// the way Windows does, a while later as an application would, and says
// what came of it: a WindowChanged, then a frame of the window's new size
// by the pixel path. A press where Windows would begin a move or size loop
// is answered with MinMax and MoveSizeStart, its release with MoveSizeEnd.
class FakeGuest {
public:
    struct Timing {
        // From a request's arrival to the window being as asked: the
        // application laying itself out.
        std::chrono::milliseconds answer{25};
        // From then to a frame of that size: the capture.
        std::chrono::milliseconds frame{16};
    };

    // What the host asked, as it came.
    struct Asked {
        std::int64_t at_ns = 0;
        std::variant<Rect, SysCommand::Command> request;
    };
    // A frame the window was said to have.
    struct Placed {
        std::int64_t at_ns = 0;
        Rect frame;
    };
    // A picture sent, and its size.
    struct Pictured {
        std::int64_t at_ns = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    FakeGuest(host::EventLoop& loop, ModelWindow& window, Timing timing);
    ~FakeGuest();

    FakeGuest(const FakeGuest&) = delete;
    FakeGuest& operator=(const FakeGuest&) = delete;
    FakeGuest(FakeGuest&&) = delete;
    FakeGuest& operator=(FakeGuest&&) = delete;

    // Dials the host, says hello and the window. False when nothing answers.
    [[nodiscard]] bool dial(std::uint16_t port);
    [[nodiscard]] WindowId window_id() const noexcept { return id_; }
    // The host has asked for the window's frames.
    [[nodiscard]] bool subscribed() const noexcept { return subscribed_; }
    [[nodiscard]] bool connected() const noexcept { return fd_ >= 0; }

    // The window does something by itself in the guest: its user pressed one
    // of its caption's buttons, or the application moved it.
    void act(SysCommand::Command command);
    void place_itself(const Rect& frame);

    [[nodiscard]] const std::vector<Asked>& asked() const noexcept { return asked_; }
    [[nodiscard]] const std::vector<Placed>& placed() const noexcept { return placed_; }
    [[nodiscard]] const std::vector<Pictured>& pictured() const noexcept { return pictured_; }
    [[nodiscard]] std::size_t loops_begun() const noexcept { return loops_begun_; }
    [[nodiscard]] std::size_t loops_ended() const noexcept { return loops_ended_; }

private:
    using Request = std::variant<Rect, SysCommand::Command, MoveSizeKind>;
    struct Due {
        std::chrono::steady_clock::time_point at;
        Request request;
    };

    void read_host();
    void take(MessageReader& message);
    void ask(Request request);
    void carry_out();
    void apply(const Request& request);
    void report();
    void send_frame();
    void send(const std::vector<std::uint8_t>& bytes);

    host::EventLoop& loop_;
    ModelWindow& window_;
    Timing timing_;
    WindowId id_{0x10a42, 1};
    int fd_ = -1;
    std::unique_ptr<host::EventLoop::Watch> watch_;
    std::unique_ptr<host::EventLoop::Timer> carry_;
    std::unique_ptr<host::EventLoop::Timer> capture_;
    FrameDecoder decoder_;
    std::vector<std::uint8_t> chunk_;
    std::vector<std::uint8_t> pixels_;
    std::deque<Due> due_;
    WindowFields said_;
    bool subscribed_ = false;
    std::optional<MoveSizeKind> looping_;
    std::vector<Asked> asked_;
    std::vector<Placed> placed_;
    std::vector<Pictured> pictured_;
    std::size_t loops_begun_ = 0;
    std::size_t loops_ended_ = 0;
};

}  // namespace rwin::integration
