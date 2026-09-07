#include "guest/guest_session.hpp"
#include "guest/tcp_client.hpp"
#include "log/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

constexpr std::uint16_t kDefaultPort = 5140;
constexpr std::uint32_t kDefaultFramesPerSecond = 30;
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
    const std::string host = option(argc, argv, "--host", kDefaultHost);
    const auto port = static_cast<std::uint16_t>(
        std::strtoul(option(argc, argv, "--port", std::to_string(kDefaultPort)).c_str(),
                     nullptr, 10));
    const auto fps = static_cast<std::uint32_t>(
        std::strtoul(option(argc, argv, "--fps",
                            std::to_string(kDefaultFramesPerSecond)).c_str(),
                     nullptr, 10));

    rwin::Logger::instance().opened("remotewin-guest");
    RWIN_INFO("boot", "host={} port={} fps={}", host, port, fps);

    rwin::guest::TcpClient link;
    if (!link.connect(host, port)) {
        return EXIT_FAILURE;
    }
    rwin::guest::GuestSession session{link, fps};
    session.run();
    RWIN_INFO("boot", "session ended");
    return EXIT_SUCCESS;
}
