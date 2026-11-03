#include "guest/input/input_relay.hpp"
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
constexpr std::uint32_t kDefaultFramesPerSecond = 60;
constexpr const char* kDefaultHost = "10.0.2.2";

std::string option(int argc, char** argv, const char* name, std::string fallback) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string{argv[i]} == name) return argv[i + 1];
    }
    return fallback;
}

bool flag(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::string{argv[i]} == name) return true;
    }
    return false;
}

std::uint32_t own_session() {
    DWORD session = 0;
    ::ProcessIdToSessionId(::GetCurrentProcessId(), &session);
    return session;
}

}

int main(int argc, char** argv) {
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

    if (const char* log = std::getenv("RWIN_LOG_FILE"); log != nullptr && *log != '\0') {
        if (std::freopen(log, "a", stderr) == nullptr) return EXIT_FAILURE;
    }
    rwin::Logger::instance().opened("remotewin-guest");
    if (flag(argc, argv, "--input-relay")) {
        const auto session = static_cast<std::uint32_t>(
            std::strtoul(option(argc, argv, "--session", std::to_string(own_session())).c_str(), nullptr, 10));
        RWIN_INFO("boot", "input relay for session {}", session);
        return rwin::guest::InputRelay{session}.run();
    }
    RWIN_INFO("boot", "host={} port={} fps={}", host, port, fps);
    if (unaware != ERROR_SUCCESS) {
        RWIN_WARN("boot", "cannot declare per-monitor DPI awareness (error {}); window "
                  "sizes will not match frames on a scaled guest",
                  static_cast<unsigned long>(unaware));
    }

    rwin::guest::TcpClient link;
    rwin::guest::GuestSession session{link, fps};
    if (!link.connect(host, port)) {
        return EXIT_FAILURE;
    }
    session.run();
    RWIN_INFO("boot", "session ended");
    return EXIT_SUCCESS;
}
