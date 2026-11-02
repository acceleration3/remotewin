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

class FakeGuest {
public:
    struct Timing {
        std::chrono::milliseconds answer{25};
        std::chrono::milliseconds frame{16};
    };

    struct Asked {
        std::int64_t at_ns = 0;
        std::variant<Rect, SysCommand::Command> request;
        WindowId window;
    };
    struct Placed {
        std::int64_t at_ns = 0;
        Rect frame;
    };
    struct Pictured {
        std::int64_t at_ns = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    FakeGuest(host::EventLoop& loop, ModelWindow& window, Timing timing, FakeExportDevice* device);
    ~FakeGuest();

    FakeGuest(const FakeGuest&) = delete;
    FakeGuest& operator=(const FakeGuest&) = delete;
    FakeGuest(FakeGuest&&) = delete;
    FakeGuest& operator=(FakeGuest&&) = delete;

    [[nodiscard]] bool dial(std::uint16_t port);
    [[nodiscard]] WindowId window_id() const noexcept { return id_; }
    [[nodiscard]] bool subscribed() const noexcept { return subscribed_; }
    [[nodiscard]] bool connected() const noexcept { return fd_ >= 0; }

    void act(SysCommand::Command command);
    void place_itself(const Rect& frame);

    void show_window(WindowId id, const WindowFields& fields);
    void change_window(WindowId id, const WindowFields& fields);
    void forget_window(WindowId id);
    void stack(const ZOrder& order);

    [[nodiscard]] const std::vector<Activate>& activations() const noexcept { return activations_; }
    [[nodiscard]] bool subscribed_to(WindowId id) const;
    [[nodiscard]] std::size_t rings_made(WindowId id) const;
    [[nodiscard]] bool sent_back_to_pixels(WindowId id) const;
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
    bool shared_ = false;
    std::uint32_t rings_ = 0;
    std::uint64_t surfaces_ = 0;
    std::optional<MoveSizeKind> looping_;
    bool stale_frame_ = false;
    std::vector<Other> others_;
    std::size_t shown_ = 0;
    std::vector<Activate> activations_;
    std::vector<Asked> asked_;
    std::vector<Placed> placed_;
    std::vector<Pictured> pictured_;
    std::size_t loops_begun_ = 0;
    std::size_t loops_ended_ = 0;
};

}
