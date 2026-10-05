#include "host/export/export_socket.hpp"

#include "host/export/export_request.hpp"
#include "logging/logger.hpp"

#include <array>
#include <cerrno>
#include <cstring>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace rwin::host {

std::optional<ExportSocket> ExportSocket::connect(const std::string& path, std::string& why) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.empty() || path.size() >= sizeof address.sun_path) {
        why = "'" + path + "' is no socket path";
        return std::nullopt;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    UniqueFd socket{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (!socket.valid()) {
        why = std::string("no socket: ") + std::strerror(errno);
        return std::nullopt;
    }
    // A unix socket connects or refuses at once, non-blocking or not.
    if (::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        why = "connecting to '" + path + "': " + std::strerror(errno);
        return std::nullopt;
    }
    return ExportSocket{std::move(socket)};
}

bool ExportSocket::send(const ExportRequest& request) const {
    const std::array<std::uint8_t, ExportRequest::kBytes> bytes = request.encode();
    const ssize_t sent = ::send(socket_.get(), bytes.data(), bytes.size(), MSG_NOSIGNAL);
    if (sent == static_cast<ssize_t>(bytes.size())) return true;
    RWIN_WARN("export", "a request to the GPU device was not sent: {}",
              sent < 0 ? std::strerror(errno) : "cut short");
    return false;
}

std::optional<ExportSocket::Received> ExportSocket::receive(bool& gone) const {
    // One byte more than an answer, so one that is longer shows as longer
    // rather than as an answer cut to size.
    std::array<std::uint8_t, ExportAnswer::kBytes + 1> bytes{};
    iovec vector{bytes.data(), bytes.size()};
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();
    const ssize_t received = ::recvmsg(socket_.get(), &message, MSG_CMSG_CLOEXEC | MSG_DONTWAIT);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return std::nullopt;
    UniqueFd descriptor;
    for (cmsghdr* header = CMSG_FIRSTHDR(&message); header != nullptr; header = CMSG_NXTHDR(&message, header)) {
        if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS) continue;
        int fd = -1;
        std::memcpy(&fd, CMSG_DATA(header), sizeof fd);
        descriptor = UniqueFd{fd};
    }
    if (received <= 0) {
        RWIN_WARN("export", "the GPU device closed the export connection{}{}", received < 0 ? ": " : "",
                  received < 0 ? std::strerror(errno) : "");
        gone = true;
        return std::nullopt;
    }
    const std::optional<ExportAnswer> answer =
        (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) != 0
            ? std::nullopt
            : ExportAnswer::decode(std::span<const std::uint8_t>(bytes.data(), static_cast<std::size_t>(received)));
    if (!answer.has_value()) {
        RWIN_WARN("export", "the GPU device sent {} bytes that are no answer of version 1", received);
        gone = true;
        return std::nullopt;
    }
    return Received{*answer, std::move(descriptor)};
}

bool ExportSocket::wait(int milliseconds) const {
    pollfd watched{socket_.get(), POLLIN, 0};
    return ::poll(&watched, 1, milliseconds) == 1 && (watched.revents & POLLIN) != 0;
}

}  // namespace rwin::host
