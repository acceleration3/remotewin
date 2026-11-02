#pragma once

#include "child_process.hpp"
#include "fake_export_device.hpp"
#include "fake_guest.hpp"
#include "fake_pointer.hpp"
#include "host_report.hpp"
#include "kwin_view.hpp"
#include "model_window.hpp"
#include "screen_capture.hpp"

#include "host/dbus/bus_connection.hpp"
#include "host/loop/poll_loop.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace rwin::integration {
class TestDesktop {
public:
    struct Layout {
        int width = 1600;
        int height = 1000;
        double scale = 1.2;
        FakeGuest::Timing timing;
        bool gpu_frames = false;
    };

    TestDesktop(const Layout& layout, const std::string& host_binary, const std::string& scratch);
    ~TestDesktop();

    TestDesktop(const TestDesktop&) = delete;
    TestDesktop& operator=(const TestDesktop&) = delete;
    TestDesktop(TestDesktop&&) = delete;
    TestDesktop& operator=(TestDesktop&&) = delete;

    [[nodiscard]] bool ready() const noexcept { return ready_; }

    [[nodiscard]] host::PollLoop& loop() noexcept { return loop_; }
    [[nodiscard]] ModelWindow& window() noexcept { return *window_; }
    [[nodiscard]] FakeGuest& guest() noexcept { return *guest_; }
    [[nodiscard]] FakePointer& pointer() noexcept { return *pointer_; }
    [[nodiscard]] KWinView& kwin() noexcept { return *kwin_; }
    [[nodiscard]] HostReport& host_report() noexcept { return *report_; }
    [[nodiscard]] ScreenCapture& screen() noexcept { return *screen_; }
    [[nodiscard]] bool gpu_frames_shown() const noexcept { return device_ != nullptr && device_->showable(); }
    [[nodiscard]] double scale() const noexcept { return layout_.scale; }
    [[nodiscard]] const std::string& host_log() const noexcept { return host_log_; }

    [[nodiscard]] double logical(std::int32_t pixels) const noexcept { return pixels / layout_.scale; }

    [[nodiscard]] std::optional<std::chrono::milliseconds> settle(const std::function<bool()>& condition,
                                                                  std::chrono::milliseconds timeout,
                                                                  std::chrono::milliseconds hold);
    void pass(std::chrono::milliseconds time);

private:
    bool start_kwin(const std::string& scratch);
    void write_output_config(const std::string& config) const;
    bool start_export(const std::string& scratch);
    bool start_host(const std::string& host_binary);
    bool connect_guest();

    Layout layout_;
    std::string socket_;
    std::string host_log_;
    host::PollLoop loop_;
    std::unique_ptr<ChildProcess> kwin_process_;
    std::unique_ptr<host::BusConnection> bus_;
    std::unique_ptr<FakePointer> pointer_;
    std::unique_ptr<FakeExportDevice> device_;
    std::unique_ptr<ChildProcess> host_;
    std::unique_ptr<host::EventLoop::Watch> host_output_;
    std::uint16_t port_ = 0;
    std::unique_ptr<KWinView> kwin_;
    std::unique_ptr<HostReport> report_;
    std::unique_ptr<ScreenCapture> screen_;
    std::unique_ptr<ModelWindow> window_;
    std::unique_ptr<FakeGuest> guest_;
    bool ready_ = false;
};

}
