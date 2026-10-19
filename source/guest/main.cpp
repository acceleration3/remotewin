#include "guest/session/guest_session.hpp"
#include "guest/session/tcp_client.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 5140;
// The host's refresh, not a budget: a frame is sent only when the
// compositor composed a new one, so a still picture costs nothing at any
// rate, and a moving one is as smooth as the host's glass.
constexpr std::uint32_t kDefaultFramesPerSecond = 60;
// QEMU's user-mode networking puts the host here, with no forwarding rule
// to declare and nothing to arrange before the guest boots.
constexpr const char* kDefaultHost = "10.0.2.2";

std::string option(int argc, char** argv, const char* name, std::string fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string{argv[i]} == name) return argv[i + 1];
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    // Every number this sends is a physical pixel, and so is every number
    // it is sent. The capture's frames are physical whatever the process
    // declares; a process that declares nothing is shown everything else -
    // window rectangles, the work area, the screen it moves the pointer
    // over - scaled down to 96 DPI, and on a guest set to 120% a window
    // record then says 833 where the frame is 1000. Declared before any of it
    // is asked, because the awareness is fixed from the first window call.
    // The error is kept here: the logger is not open yet, and opening it
    // sets its own.
    const DWORD unaware =
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE
            ? ERROR_SUCCESS
            : ::GetLastError();
    const std::string host = option(argc, argv, "--host", kDefaultHost);
    const auto port = static_cast<std::uint16_t>(
        std::strtoul(option(argc, argv, "--port", std::to_string(kDefaultPort)).c_str(),
                     nullptr, 10));
    const auto fps = static_cast<std::uint32_t>(
        std::strtoul(option(argc, argv, "--fps",
                            std::to_string(kDefaultFramesPerSecond)).c_str(),
                     nullptr, 10));

    // Started by the agent with no console: the agent names the file in
    // RWIN_LOG_FILE, and stderr is all a logger has.
    if (const char* log = std::getenv("RWIN_LOG_FILE"); log != nullptr && *log != '\0') {
        if (std::freopen(log, "w", stderr) == nullptr) return EXIT_FAILURE;
    }
    rwin::Logger::instance().opened("remotewin-guest");
    RWIN_INFO("boot", "host={} port={} fps={}", host, port, fps);
    if (unaware != ERROR_SUCCESS) {
        RWIN_WARN("boot", "cannot declare per-monitor DPI awareness (error {}); window "
                  "sizes will not match frames on a scaled guest",
                  static_cast<unsigned long>(unaware));
    }

    // The session - its Direct3D device above all - is made before the
    // host is dialled: on a virtual GPU making the device has taken 46 s,
    // and a host told the guest was there would have asked it for windows
    // all that while and heard nothing.
    rwin::guest::TcpClient link;
    rwin::guest::GuestSession session{link, fps};
    if (!link.connect(host, port)) {
        return EXIT_FAILURE;
    }
    session.run();
    RWIN_INFO("boot", "session ended");
    return EXIT_SUCCESS;
}
