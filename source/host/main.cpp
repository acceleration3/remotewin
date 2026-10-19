#include "host/dbus/bus_connection.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/parent_control.hpp"
#include "host/loop/poll_loop.hpp"
#include "host/mirror/mirror_scope.hpp"
#include "host/mirror/mirror_session.hpp"
#include "host/mirror/monitor_session.hpp"
#include "host/mirror/window_report.hpp"
#include "host/wayland/activation.hpp"
#include "host/wayland/compositor_gpu.hpp"
#include "host/wayland/drm_sync_device.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/seat.hpp"
#include "host/wayland/wayland_display.hpp"
#include "logging/logger.hpp"

#include <wayland-client.h>

#include <getopt.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <csignal>

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The exit status when the guest half that dialled in is from another
// build (its hello lacks what a mirror needs): whoever spawned this can tell
// the user to replace it rather than report a mirror that merely ended.
constexpr int kExitWireMismatch = 3;
// The guest half went away.
constexpr int kExitDisconnected = 4;
constexpr std::uint16_t kDefaultPort = 5140;

constexpr const char* kUsage = R"(Usage: remotewin [options]
Mirror a guest Windows application's windows, or the guest's whole monitor.
Exits with status 3 when the guest half is from another build.

  --port <port>            Port the guest dials in to (5140; 0 for any).
  --export-socket <path>   The GPU device's export socket (virtio-gpu-av-pci's
                           export-socket=): with it, and on the GPU the
                           compositor composes on, frames stay on the GPU;
                           without it they come as pixels. Defaults to
                           $RWIN_EXPORT_SOCKET.
  --pid <pid>              The launched guest process: its windows are the
                           application's.
  --image <path>           The launched program's path in the guest: windows of
                           any process of it are the application's.
  --aumid <id>             An AppUserModelID the application is known by
                           (repeatable). With none of --pid, --image and
                           --aumid, every guest window is mirrored.
  --app-id <id>            Desktop identity these windows present as.
  --desktop                Mirror the guest's whole primary monitor as one
                           window, rather than an application's windows.
  --title <title>          The title of the --desktop window.
  --managed                Report LISTENING <port>, READY and APP <aumid> on
                           stdout; accept RAISE [token] on stdin. Exit when the
                           parent closes stdin.
  --help                   This.
  --version                The wire spoken.

$XDG_ACTIVATION_TOKEN, when set, activates the first window shown.
)";

struct Options {
    std::uint16_t port = kDefaultPort;
    std::string export_socket;
    rwin::host::MirrorScope::Launch launch;
    std::string app_id;
    bool desktop = false;
    std::string title = "Guest desktop";
    bool managed = false;
};

enum Option : int { kPort = 1, kExport, kPid, kImage, kAumid, kAppId, kDesktop, kTitle, kManaged, kHelp, kVersion };

template <typename Number>
bool parse_number(std::string_view text, Number& into) {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), into);
    return error == std::errc{} && end == text.data() + text.size();
}

// Nothing when the program is to exit at once, with the status given.
std::optional<Options> parse(int argc, char** argv, int& status) {
    static const option kOptions[] = {
        {"port", required_argument, nullptr, kPort},     {"export-socket", required_argument, nullptr, kExport},
        {"pid", required_argument, nullptr, kPid},       {"image", required_argument, nullptr, kImage},
        {"aumid", required_argument, nullptr, kAumid},   {"app-id", required_argument, nullptr, kAppId},
        {"desktop", no_argument, nullptr, kDesktop},     {"title", required_argument, nullptr, kTitle},
        {"managed", no_argument, nullptr, kManaged},     {"help", no_argument, nullptr, kHelp},
        {"version", no_argument, nullptr, kVersion},     {nullptr, 0, nullptr, 0},
    };
    Options options;
    if (const char* socket = std::getenv("RWIN_EXPORT_SOCKET")) options.export_socket = socket;
    for (int chosen = 0; (chosen = ::getopt_long(argc, argv, "", kOptions, nullptr)) != -1;) {
        const std::string_view value = optarg != nullptr ? optarg : "";
        std::uint32_t pid = 0;
        switch (chosen) {
            case kPort:
                if (!parse_number(value, options.port)) {
                    std::fprintf(stderr, "Invalid --port\n");
                    status = EXIT_FAILURE;
                    return std::nullopt;
                }
                break;
            case kExport: options.export_socket = value; break;
            case kPid:
                if (!parse_number(value, pid)) {
                    std::fprintf(stderr, "Invalid --pid\n");
                    status = EXIT_FAILURE;
                    return std::nullopt;
                }
                options.launch.process_id = pid;
                break;
            case kImage: options.launch.image = value; break;
            case kAumid: options.launch.aumids.emplace_back(value); break;
            case kAppId: options.app_id = value; break;
            case kDesktop: options.desktop = true; break;
            case kTitle: options.title = value; break;
            case kManaged: options.managed = true; break;
            case kHelp:
                std::fputs(kUsage, stdout);
                status = EXIT_SUCCESS;
                return std::nullopt;
            case kVersion:
                std::puts("remotewin wire 6");
                status = EXIT_SUCCESS;
                return std::nullopt;
            default:
                std::fputs(kUsage, stderr);
                status = EXIT_FAILURE;
                return std::nullopt;
        }
    }
    return options;
}

// Taken once, by the first window: a token is good for one activation, and
// a process this one starts must not take it too.
std::string take_activation_token() {
    const char* token = std::getenv("XDG_ACTIVATION_TOKEN");
    std::string taken = token != nullptr ? token : "";
    ::unsetenv("XDG_ACTIVATION_TOKEN");
    return taken;
}

// SIGTERM, SIGINT and SIGHUP end the loop rather than the process, so what
// is undone on the way out is undone: the scripts loaded into KWin are
// unloaded and their files removed.
class Stopping {
public:
    explicit Stopping(rwin::host::PollLoop& loop) {
        sigset_t signals;
        sigemptyset(&signals);
        for (const int signal : {SIGTERM, SIGINT, SIGHUP}) sigaddset(&signals, signal);
        ::sigprocmask(SIG_BLOCK, &signals, nullptr);
        fd_ = ::signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
        if (fd_ < 0) return;
        watch_ = loop.watch(fd_, POLLIN, [this, &loop](short) {
            signalfd_siginfo info{};
            if (::read(fd_, &info, sizeof info) != sizeof info) return;
            RWIN_INFO("boot", "signal {}: stopping", info.ssi_signo);
            loop.quit(EXIT_SUCCESS);
        });
    }
    ~Stopping() {
        watch_.reset();
        if (fd_ >= 0) ::close(fd_);
    }
    Stopping(const Stopping&) = delete;
    Stopping& operator=(const Stopping&) = delete;

private:
    int fd_ = -1;
    std::unique_ptr<rwin::host::EventLoop::Watch> watch_;
};

int run(const Options& options) {
    using namespace rwin::host;
    PollLoop loop;
    const Stopping stopping{loop};
    const WaylandDisplay display{loop};
    const Globals globals{display.display()};
    Seat seat{loop, globals};
    // The seat's pointer and keyboard are said as it is bound.
    wl_display_roundtrip(display.display());
    const CompositorGpu gpu{globals};
    const DrmSyncDevice sync{gpu.render_node(), globals.syncobj()};
    BusConnection bus{loop};
    GuestLink link{loop};
    // Opened before the guest dials in: it is told at its hello which way
    // its frames are to come.
    SharedFrameFeed frames{loop, link};
    if (!options.export_socket.empty()) {
        frames.open(options.export_socket, gpu.pci());
    } else {
        RWIN_INFO("boot", "no export socket: frames come as pixels");
    }
    if (!link.listen(options.port, options.managed)) return EXIT_FAILURE;
    std::unique_ptr<ParentControl> control;
    if (options.managed) {
        control = std::make_unique<ParentControl>(loop, STDIN_FILENO, STDOUT_FILENO);
        // Losing the parent only detaches the mirror: the guest's windows
        // are not asked to close.
        control->closed.connect([&loop] { loop.quit(EXIT_SUCCESS); });
        control->failed.connect([&loop] { loop.quit(EXIT_FAILURE); });
        control->report_listening(link.port());
    }
    // A guest of another build is disconnected as it is refused; the
    // disconnection is not what the exit says.
    bool mismatched = false;
    link.mismatched.connect([&](const std::string&) {
        mismatched = true;
        loop.quit(kExitWireMismatch);
    });
    link.disconnected.connect([&] {
        if (!mismatched) loop.quit(kExitDisconnected);
    });
    const SurfaceKit surfaces{globals, loop, seat, sync};
    if (options.desktop) {
        RWIN_INFO("boot", "mirroring the guest's monitor as '{}'", options.title);
        Activation activation{globals, seat};
        MonitorSession session{surfaces, link, frames, activation, options.title, options.app_id,
                               take_activation_token()};
        if (control) {
            session.ready.connect([&control] { control->report_ready(); });
            control->raise_requested.connect([&session](const std::string& token) { session.raise(token); });
        }
        session.finished.connect([&loop] { loop.quit(EXIT_SUCCESS); });
        return loop.run();
    }
    RWIN_INFO("boot", "mirroring pid {} image '{}' and {} known app id(s)",
              options.launch.process_id.has_value() ? std::to_string(*options.launch.process_id) : std::string("-"),
              options.launch.image, options.launch.aumids.size());
    MirrorScope scope{options.launch};
    if (scope.everything()) RWIN_INFO("boot", "nothing to go by: every guest window is mirrored");
    MirrorSession session{MirrorSession::Parts{loop, link, frames, bus, surfaces}, std::move(scope), options.app_id,
                          take_activation_token()};
    const WindowReport report{bus, session};
    if (control) {
        session.ready.connect([&control] { control->report_ready(); });
        session.app_claimed.connect([&control](const std::string& aumid) { control->report_app(aumid); });
        control->raise_requested.connect([&session](const std::string& token) { session.raise(token); });
    }
    session.finished.connect([&loop] { loop.quit(EXIT_SUCCESS); });
    return loop.run();
}

}  // namespace

int main(int argc, char** argv) {
    int status = EXIT_SUCCESS;
    const std::optional<Options> options = parse(argc, argv, status);
    if (!options.has_value()) return status;
    rwin::Logger::instance().opened("remotewin");
    RWIN_INFO("boot", "wire 6");
    try {
        return run(*options);
    } catch (const std::exception& error) {
        RWIN_ERROR("boot", "{}", error.what());
        std::fprintf(stderr, "remotewin: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
