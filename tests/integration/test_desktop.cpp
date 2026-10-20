#include "test_desktop.hpp"

#include "host/placement/kwin_script.hpp"
#include "host/wayland/compositor_gpu.hpp"
#include "host/wayland/globals.hpp"

#include <wayland-client.h>

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <format>
#include <thread>

namespace rwin::integration {

namespace {

constexpr auto kStartWait = std::chrono::milliseconds(20000);
constexpr auto kTick = std::chrono::milliseconds(2);
constexpr auto kNamePoll = std::chrono::milliseconds(20);
// The guest's taskbar, along the bottom of its screen.
constexpr int kTaskbarHeight = 48;

ModelWindow::Shape window_shape(int screen_width, int screen_height) {
    ModelWindow::Shape shape;
    shape.frame = Rect{240, 180, 960, 720};
    shape.work_area = Rect{0, 0, static_cast<std::uint32_t>(screen_width),
                           static_cast<std::uint32_t>(screen_height - kTaskbarHeight)};
    shape.min_width = 300;
    shape.min_height = 200;
    shape.margin_side = 7;
    shape.margin_bottom = 7;
    shape.top_strip = 6;
    shape.caption_height = 36;
    shape.title = "Integration window";
    return shape;
}

}  // namespace

TestDesktop::TestDesktop(const Layout& layout, const std::string& host_binary, const std::string& scratch)
    : layout_(layout), socket_(std::format("remotewin-integration-{}", ::getpid())),
      host_log_(scratch + "/host.log") {
    window_ = std::make_unique<ModelWindow>(window_shape(static_cast<int>(std::lround(layout.width * layout.scale)),
                                                         static_cast<int>(std::lround(layout.height * layout.scale))));
    if (!start_kwin(scratch) || !start_export(scratch) || !start_host(host_binary) || !connect_guest()) return;
    const Rect frame = window_->frame();
    const auto mapped = settle(
        [&] {
            const auto& now = kwin_->now();
            return now.has_value() && std::abs(now->width - logical(static_cast<std::int32_t>(frame.width))) < 1.0
                   && std::abs(now->x - logical(frame.x)) < 1.0 && std::abs(now->y - logical(frame.y)) < 1.0;
        },
        kStartWait, std::chrono::milliseconds(100));
    if (!mapped.has_value()) {
        std::fprintf(stderr, "setup: the guest window was never mapped where the guest has it\n");
        return;
    }
    ready_ = true;
}

TestDesktop::~TestDesktop() {
    guest_.reset();
    device_.reset();
    kwin_.reset();
    host_output_.reset();
    if (host_ != nullptr) host_->stop();
    pointer_.reset();
    bus_.reset();
    if (kwin_process_ != nullptr) kwin_process_->stop();
}

bool TestDesktop::start_kwin(const std::string& scratch) {
    const std::string config = scratch + "/config";
    std::filesystem::create_directories(config);
    write_output_config(config);
    kwin_process_ = std::make_unique<ChildProcess>(ChildProcess::Launch{
        {"kwin_wayland", "--virtual", "--no-lockscreen", "--socket", socket_, "--width", std::to_string(layout_.width),
         "--height", std::to_string(layout_.height), "--scale", std::format("{}", layout_.scale)},
        {{"XDG_CONFIG_HOME", config}, {"QT_LOGGING_RULES", "*.debug=false"}},
        scratch + "/kwin.log",
        false});
    bus_ = std::make_unique<host::BusConnection>(loop_);
    const auto until = std::chrono::steady_clock::now() + kStartWait;
    while (!host::KWinScript::reachable(*bus_)) {
        if (std::chrono::steady_clock::now() > until || !kwin_process_->running()) {
            std::fprintf(stderr, "setup: KWin never came on the bus (see %s/kwin.log)\n", scratch.c_str());
            return false;
        }
        std::this_thread::sleep_for(kNamePoll);
    }
    pointer_ = std::make_unique<FakePointer>(socket_);
    if (!pointer_->usable()) {
        std::fprintf(stderr, "setup: KWin offers no fake input\n");
        return false;
    }
    return true;
}

// KWin's virtual output is made at --width and --height times --scale, and
// then scaled as its output configuration says - which, written afresh,
// says 1. Said here first, it is the scale asked for.
void TestDesktop::write_output_config(const std::string& config) const {
    const std::string json = std::format(
        R"([{{"name": "outputs", "data": [{{"connectorName": "Virtual-0", "scale": {},
             "mode": {{"basic": {{"width": {}, "height": {}, "refreshRate": 60000}}, "flags": 1}}}}]}},
            {{"name": "setups", "data": [{{"lidClosed": false, "outputs": [{{"enabled": true, "outputIndex": 0,
             "position": {{"x": 0, "y": 0}}, "priority": 0, "replicationSource": ""}}]}}]}}])",
        layout_.scale, std::lround(layout_.width * layout_.scale), std::lround(layout_.height * layout_.scale));
    std::FILE* file = std::fopen((config + "/kwinoutputconfig.json").c_str(), "w");
    if (file == nullptr) return;
    std::fputs(json.c_str(), file);
    std::fclose(file);
}

// The device names the GPU the compositor composes on, as accel-virt's
// does on a desktop it serves: the host takes its surfaces only then.
bool TestDesktop::start_export(const std::string& scratch) {
    if (!layout_.gpu_frames) return true;
    wl_display* display = wl_display_connect(socket_.c_str());
    if (display == nullptr) {
        std::fprintf(stderr, "setup: cannot connect to KWin to ask its GPU\n");
        return false;
    }
    std::optional<host::PciAddress> gpu;
    {
        const host::Globals globals{display};
        gpu = host::CompositorGpu{globals}.pci();
    }
    wl_display_disconnect(display);
    device_ = std::make_unique<FakeExportDevice>(loop_, scratch + "/export.sock", gpu);
    return device_->listening();
}

bool TestDesktop::start_host(const std::string& host_binary) {
    // RWIN_INTEGRATION_HOST_LOG picks the host's log level (debug by
    // default); RWIN_INTEGRATION_WAYLAND_DEBUG=1 adds its Wayland traffic.
    const char* level = std::getenv("RWIN_INTEGRATION_HOST_LOG");
    std::vector<std::pair<std::string, std::string>> env{{"WAYLAND_DISPLAY", socket_},
                                                         {"RWIN_LOG", level != nullptr ? level : "debug"}};
    if (const char* traffic = std::getenv("RWIN_INTEGRATION_WAYLAND_DEBUG"); traffic != nullptr && *traffic == '1') {
        env.emplace_back("WAYLAND_DEBUG", "client");
    }
    std::vector<std::string> arguments{host_binary, "--managed", "--port", "0"};
    if (device_ != nullptr) {
        arguments.emplace_back("--export-socket");
        arguments.push_back(device_->path());
    }
    host_ = std::make_unique<ChildProcess>(ChildProcess::Launch{std::move(arguments), std::move(env), host_log_, true});
    std::string said;
    host_output_ = loop_.watch(host_->output(), POLLIN, [this, &said](short) {
        std::array<char, 256> chunk{};
        const ssize_t got = ::read(host_->output(), chunk.data(), chunk.size());
        if (got <= 0) {
            host_output_.reset();
            return;
        }
        said.append(chunk.data(), static_cast<std::size_t>(got));
        const std::size_t at = said.find("LISTENING ");
        const std::size_t end = said.find('\n', at);
        if (port_ == 0 && at != std::string::npos && end != std::string::npos) {
            port_ = static_cast<std::uint16_t>(std::stoi(said.substr(at + 10, end - at - 10)));
        }
    });
    const auto listening = settle([this] { return port_ != 0 || !host_->running(); }, kStartWait, kTick);
    host_output_ = loop_.watch(host_->output(), POLLIN, [this](short) {
        std::array<char, 256> chunk{};
        if (::read(host_->output(), chunk.data(), chunk.size()) <= 0) host_output_.reset();
    });
    if (!listening.has_value() || port_ == 0) {
        std::fprintf(stderr, "setup: the host never listened (see %s)\n", host_log_.c_str());
        return false;
    }
    kwin_ = std::make_unique<KWinView>(loop_, *bus_, host_->pid());
    report_ = std::make_unique<HostReport>(*bus_, host_->pid());
    if (!settle([this] { return kwin_->watching(); }, kStartWait, kTick).has_value()) {
        std::fprintf(stderr, "setup: KWin never ran the watching script\n");
        return false;
    }
    return true;
}

bool TestDesktop::connect_guest() {
    guest_ = std::make_unique<FakeGuest>(loop_, *window_, layout_.timing, device_.get());
    return guest_->dial(port_);
}

std::optional<std::chrono::milliseconds> TestDesktop::settle(const std::function<bool()>& condition,
                                                             std::chrono::milliseconds timeout,
                                                             std::chrono::milliseconds hold) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point began = Clock::now();
    std::optional<Clock::time_point> holding_since;
    std::optional<std::chrono::milliseconds> took;
    const auto check = loop_.timer([&] {
        const Clock::time_point now = Clock::now();
        if (!condition()) {
            holding_since.reset();
        } else if (!holding_since.has_value()) {
            holding_since = now;
        }
        if (holding_since.has_value() && now - *holding_since >= hold) {
            took = std::chrono::duration_cast<std::chrono::milliseconds>(*holding_since - began);
            loop_.quit(0);
        } else if (now - began > timeout) {
            loop_.quit(1);
        }
    });
    check->start(kTick, true);
    loop_.run();
    return took;
}

void TestDesktop::pass(std::chrono::milliseconds time) {
    const auto done = loop_.timer([this] { loop_.quit(0); });
    done->start(time, false);
    loop_.run();
}

}  // namespace rwin::integration
