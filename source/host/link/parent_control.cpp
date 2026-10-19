#include "host/link/parent_control.hpp"

#include "logging/logger.hpp"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>

namespace rwin::host {

namespace {

constexpr std::size_t kLongestCommand = 1024;
constexpr std::string_view kRaise = "RAISE";

}  // namespace

ParentControl::ParentControl(EventLoop& loop, int command_fd, int status_fd)
    : command_fd_(command_fd), status_fd_(status_fd),
      commands_(loop.watch(command_fd, POLLIN, [this](short) { read_commands(); })) {}

void ParentControl::report_listening(std::uint16_t port) { report("LISTENING " + std::to_string(port)); }

void ParentControl::report_ready() { report("READY"); }

void ParentControl::report_app(std::string_view aumid) { report("APP " + std::string(aumid)); }

void ParentControl::report(std::string_view line) {
    std::string out(line);
    out += '\n';
    std::size_t written = 0;
    while (written < out.size()) {
        const ssize_t wrote = ::write(status_fd_, out.data() + written, out.size() - written);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote <= 0) {
            RWIN_ERROR("parent", "cannot write the status pipe: {}", std::strerror(errno));
            failed();
            return;
        }
        written += static_cast<std::size_t>(wrote);
    }
}

void ParentControl::read_commands() {
    std::array<char, 256> bytes{};
    const ssize_t count = ::read(command_fd_, bytes.data(), bytes.size());
    if (count < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (count <= 0) {
        commands_.reset();
        if (count == 0) {
            closed();
        } else {
            failed();
        }
        return;
    }
    pending_.append(bytes.data(), static_cast<std::size_t>(count));
    for (std::size_t end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n')) {
        const std::string line = pending_.substr(0, end);
        pending_.erase(0, end + 1);
        if (line == kRaise) {
            raise_requested(std::string());
        } else if (line.starts_with(std::string(kRaise) + ' ')) {
            raise_requested(line.substr(kRaise.size() + 1));
        } else {
            RWIN_WARN("parent", "unknown command '{}'", line);
        }
    }
    if (pending_.size() > kLongestCommand) {
        RWIN_ERROR("parent", "command exceeds {} bytes", kLongestCommand);
        commands_.reset();
        failed();
    }
}

}  // namespace rwin::host
