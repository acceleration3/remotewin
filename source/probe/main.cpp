#include "guest/session/tcp_client.hpp"
#include "logging/logger.hpp"
#include "probe/backdrop_window.hpp"
#include "probe/probe_session.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string option(int argc, char** argv, const char* name) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string{argv[i]} == name) return argv[i + 1];
    }
    return {};
}

int serve_backdrop(const std::string& colour, const std::string& parent) {
    const auto rgb = static_cast<std::uint32_t>(std::strtoul(colour.c_str(), nullptr, 16));
    const auto parent_id = static_cast<std::uint32_t>(std::strtoul(parent.c_str(), nullptr, 10));
    const rwin::probe::BackdropWindow backdrop{rgb};
    if (!backdrop.made()) return EXIT_FAILURE;
    backdrop.stay_while(parent_id);
    return EXIT_SUCCESS;
}

}

int main(int argc, char** argv) {
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    if (const std::string log = option(argc, argv, "--log"); !log.empty()) {
        if (std::freopen(log.c_str(), "w", stderr) == nullptr) return EXIT_FAILURE;
    }
    rwin::Logger::instance().opened("remotewin-probe");

    if (const std::string colour = option(argc, argv, "--backdrop"); !colour.empty()) {
        return serve_backdrop(colour, option(argc, argv, "--parent"));
    }

    const std::string harness = option(argc, argv, "--harness");
    const std::size_t colon = harness.rfind(':');
    if (colon == std::string::npos) {
        RWIN_ERROR("boot", "usage: remotewin-probe.exe --harness <ipv4>:<port> [--log <file>]");
        return EXIT_FAILURE;
    }
    const std::string host = harness.substr(0, colon);
    const auto port = static_cast<std::uint16_t>(
        std::strtoul(harness.substr(colon + 1).c_str(), nullptr, 10));

    rwin::guest::TcpClient link;
    if (!link.connect(host, port)) return EXIT_FAILURE;
    rwin::probe::ProbeSession session{link};
    session.run();
    RWIN_INFO("boot", "session ended");
    return EXIT_SUCCESS;
}
