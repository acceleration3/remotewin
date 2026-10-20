#pragma once

#include "model_window.hpp"

#include "host/loop/event_loop.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/move_size_kind.hpp"
#include "proto/rect.hpp"
#include "proto/activate.hpp"
#include "proto/sys_command.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

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

class FakeExportDevice;

// The guest half, played: one window (ModelWindow) on the other end of a
// wire 6 link to the host under test, as remotewin-guest.exe would be. It
// answers what the host asks of the window - a WindowMove, a SysCommand -
// the way Windows does, a while later as an application would, and says
// what came of it: a WindowChanged, then a frame of the window's new size
// by the pixel path. A press where Windows would begin a move or size loop
// is answered with MinMax and MoveSizeStart, its release with MoveSizeEnd.
// A window brought back from minimised sends first the frame Windows'
// capture took of it as it went, the size of its minimised stub, as
// Windows.Graphics.Capture hands one over.
//
// With a GPU device (FakeExportDevice) and the host taking shared surfaces,
// the other windows' pictures go as the real guest sends them on the GPU: a
// ring of three surfaces made on the device, and each picture copied into a
// slot of it. A window the host sends back to pixels (WindowPixels) is
// captured afresh, and its picture sent as pixels at once. The
// ModelWindow's pictures always go as pixels.
//
// Each other window's picture is a colour of its own, and what lies outside
// its region is clear (premultiplied 0), as Windows' capture hands over a
// window with one: measured on WPF's rounded WindowChrome, and on a colour
// key, whose texels come as clear too.
class FakeGuest {
public:
    struct Timing {
        // From a request's arrival to the window being as asked: the
        // application laying itself out.
        std::chrono::milliseconds answer{25};
        // From then to a frame of that size: the capture.
        std::chrono::milliseconds frame{16};
    };

    // What the host asked, as it came, and of which window: only the
    // ModelWindow's requests are carried out.
    struct Asked {
        std::int64_t at_ns = 0;
        std::variant<Rect, SysCommand::Command> request;
        WindowId window;
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

    // ``device`` is null for a guest whose frames go only as pixels.
    FakeGuest(host::EventLoop& loop, ModelWindow& window, Timing timing, FakeExportDevice* device);
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

    // Other windows of the application, said as the real guest says them:
    // whole when first shown (WindowNew), then what changed, then gone; a
    // window the host subscribed to is sent a frame of its picture's size
    // whenever it is shown and has one, as Windows' capture does.
    void show_window(WindowId id, const WindowFields& fields);
    void change_window(WindowId id, const WindowFields& fields);
    void forget_window(WindowId id);
    // The guest's stacking and active window, once Windows has settled it.
    void stack(const ZOrder& order);

    // What the host asked to be made active, or not, in the guest.
    [[nodiscard]] const std::vector<Activate>& activations() const noexcept { return activations_; }
    [[nodiscard]] bool subscribed_to(WindowId id) const;
    // Rings made for a window's pictures on the GPU device, and windows the
    // host sent back to pixels.
    [[nodiscard]] std::size_t rings_made(WindowId id) const;
    [[nodiscard]] bool sent_back_to_pixels(WindowId id) const;
    // The colour a window's picture is, 0xAARRGGBB; 0 for none known.
    [[nodiscard]] std::uint32_t colour_of(WindowId id) const;

    [[nodiscard]] const std::vector<Asked>& asked() const noexcept { return asked_; }
    [[nodiscard]] const std::vector<Placed>& placed() const noexcept { return placed_; }
    [[nodiscard]] const std::vector<Pictured>& pictured() const noexcept { return pictured_; }
    [[nodiscard]] std::size_t loops_begun() const noexcept { return loops_begun_; }
    [[nodiscard]] std::size_t loops_ended() const noexcept { return loops_ended_; }

private:
    using Request = std::variant<Rect, SysCommand::Command, MoveSizeKind>;
    struct Other {
        WindowId id;
        WindowFields said;
        std::uint32_t colour = 0;
        bool subscribed = false;
        bool pixels_only = false;
        std::uint32_t ring = 0;
        std::uint32_t ring_width = 0;
        std::uint32_t ring_height = 0;
        std::size_t rings_made = 0;
        // Slot 0's surface, and the copies into it so far.
        std::uint64_t slot_surface = 0;
        std::uint64_t copies = 0;
    };
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
    void send_picture(std::uint32_t width, std::uint32_t height);
    void send_pixels(WindowId id, std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t>& pixels);
    void picture_other(Other& other);
    // ``other``'s picture, as Windows' capture would hand it over, into
    // other_pixels_.
    void paint(const Other& other, std::uint32_t width, std::uint32_t height);
    void copy_into_ring(Other& other, std::uint32_t width, std::uint32_t height);
    [[nodiscard]] Other* other(WindowId id);
    void send(const std::vector<std::uint8_t>& bytes);

    host::EventLoop& loop_;
    ModelWindow& window_;
    Timing timing_;
    FakeExportDevice* device_;
    WindowId id_{0x10a42, 1};
    int fd_ = -1;
    std::unique_ptr<host::EventLoop::Watch> watch_;
    std::unique_ptr<host::EventLoop::Timer> carry_;
    std::unique_ptr<host::EventLoop::Timer> capture_;
    FrameDecoder decoder_;
    std::vector<std::uint8_t> chunk_;
    std::vector<std::uint8_t> pixels_;
    std::vector<std::uint8_t> other_pixels_;
    std::deque<Due> due_;
    WindowFields said_;
    bool subscribed_ = false;
    // The host takes the pictures of windows on the GPU (FrameRoute).
    bool shared_ = false;
    std::uint32_t rings_ = 0;
    std::uint64_t surfaces_ = 0;
    std::optional<MoveSizeKind> looping_;
    // The next frame is the one captured as the window was minimised.
    bool stale_frame_ = false;
    std::vector<Other> others_;
    // Other windows shown so far: each is the next colour.
    std::size_t shown_ = 0;
    std::vector<Activate> activations_;
    std::vector<Asked> asked_;
    std::vector<Placed> placed_;
    std::vector<Pictured> pictured_;
    std::size_t loops_begun_ = 0;
    std::size_t loops_ended_ = 0;
};

}  // namespace rwin::integration
