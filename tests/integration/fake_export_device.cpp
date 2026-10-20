#include "fake_export_device.hpp"

#include "host/export/export_answer.hpp"
#include "host/export/export_request.hpp"
#include "host/export/exported_layout.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace rwin::integration {

namespace {

using host::ExportAnswer;
using host::ExportRequest;
using Operation = ExportRequest::Operation;

constexpr std::size_t kBodyAt = 32;
constexpr std::size_t kBodyBytes = ExportAnswer::kBytes - kBodyAt;
constexpr std::uint8_t kOnPci = 1;

template <typename Number>
void put(std::span<std::uint8_t> bytes, std::size_t at, Number value) {
    for (std::size_t byte = 0; byte < sizeof(Number); ++byte) {
        bytes[at + byte] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * byte));
    }
}

template <typename Number>
Number get(std::span<const std::uint8_t> bytes, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(Number); ++byte) {
        value |= static_cast<std::uint64_t>(bytes[at + byte]) << (8 * byte);
    }
    return static_cast<Number>(value);
}

}  // namespace

FakeExportDevice::FakeExportDevice(host::EventLoop& loop, std::string path, std::optional<host::PciAddress> gpu)
    : loop_(loop), path_(std::move(path)), gpu_(gpu), udmabuf_(::open("/dev/udmabuf", O_RDWR | O_CLOEXEC)) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path_.size() >= sizeof address.sun_path) {
        std::fprintf(stderr, "export: '%s' is too long for a socket\n", path_.c_str());
        return;
    }
    std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);
    host::UniqueFd listener{::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (!listener.valid() || ::bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0
        || ::listen(listener.get(), 1) != 0) {
        std::fprintf(stderr, "export: cannot listen at %s: %s\n", path_.c_str(), std::strerror(errno));
        return;
    }
    listener_ = std::move(listener);
    accepting_ = loop_.watch(listener_.get(), POLLIN, [this](short) { accept(); });
}

void FakeExportDevice::made(std::uint64_t surface, std::uint32_t width, std::uint32_t height) {
    surfaces_.push_back(Surface{surface, width, height, 0, SurfaceBuffer{udmabuf_.get(), width, height}});
}

void FakeExportDevice::copied(std::uint64_t id, std::span<const std::uint8_t> picture, std::uint32_t width,
                              std::uint32_t height) {
    Surface* known = surface(id);
    if (known == nullptr) return;
    known->buffer.copy(picture, width, height);
    ++known->copies;
}

FakeExportDevice::Surface* FakeExportDevice::surface(std::uint64_t id) {
    const auto found = std::find_if(surfaces_.begin(), surfaces_.end(), [id](const Surface& s) { return s.id == id; });
    return found == surfaces_.end() ? nullptr : &*found;
}

void FakeExportDevice::accept() {
    host::UniqueFd connection{::accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK)};
    if (!connection.valid()) return;
    connection_ = std::move(connection);
    reading_ = loop_.watch(connection_.get(), POLLIN, [this](short) { read_requests(); });
}

void FakeExportDevice::read_requests() {
    std::array<std::uint8_t, ExportRequest::kBytes> request{};
    while (true) {
        const ssize_t got = ::recv(connection_.get(), request.data(), request.size(), MSG_DONTWAIT);
        if (got == static_cast<ssize_t>(request.size())) {
            answer(request);
            continue;
        }
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return;
        reading_.reset();
        connection_ = host::UniqueFd{};
        return;
    }
}

// Every request is answered, in order, as the device answers them.
void FakeExportDevice::answer(std::span<const std::uint8_t> request) {
    const auto operation = get<std::uint32_t>(request, 8);
    const auto id = get<std::uint64_t>(request, 16);
    const auto tag = get<std::uint64_t>(request, 24);
    std::array<std::uint8_t, kBodyBytes> body{};
    switch (static_cast<Operation>(operation)) {
        case Operation::Hello: {
            const host::PciAddress pci = gpu_.value_or(host::PciAddress{});
            put(std::span(body), 16, pci.domain);
            body[20] = pci.bus;
            body[21] = pci.device;
            body[22] = pci.function;
            body[23] = kOnPci;
            send(operation, ExportAnswer::Status::Ok, 0, tag, body, -1);
            return;
        }
        case Operation::Export: {
            const Surface* known = surface(id);
            if (known == nullptr || !known->buffer.valid()) break;
            put(std::span(body), 0, known->width);
            put(std::span(body), 4, known->height);
            put(std::span(body), 8, host::ExportedLayout::kArgb8888);
            put(std::span(body), 12, known->buffer.stride());
            put(std::span(body), 24, std::uint64_t{0});
            ++exports_;
            send(operation, ExportAnswer::Status::Ok, id, tag, body, known->buffer.handed());
            return;
        }
        case Operation::Sync: {
            const Surface* known = surface(id);
            if (known == nullptr) break;
            const host::UniqueFd fence{::eventfd(1, EFD_CLOEXEC)};
            put(std::span(body), 0, known->copies);
            send(operation, ExportAnswer::Status::Ok, id, tag, body, fence.get());
            return;
        }
        case Operation::Release:
        case Operation::Timing:
            send(operation, ExportAnswer::Status::Ok, id, tag, body, -1);
            return;
    }
    send(operation, ExportAnswer::Status::NotFound, id, tag, body, -1);
}

void FakeExportDevice::send(std::uint32_t operation, ExportAnswer::Status status, std::uint64_t surface,
                            std::uint64_t tag, std::span<const std::uint8_t> body, int descriptor) {
    std::array<std::uint8_t, ExportAnswer::kBytes> bytes{};
    put(std::span(bytes), 0, ExportAnswer::kMagic);
    put(std::span(bytes), 4, ExportAnswer::kMajor);
    put(std::span(bytes), 6, ExportRequest::kMinor);
    put(std::span(bytes), 8, operation);
    put(std::span(bytes), 12, static_cast<std::uint32_t>(status));
    put(std::span(bytes), 16, surface);
    put(std::span(bytes), 24, tag);
    std::copy(body.begin(), body.end(), bytes.begin() + kBodyAt);
    iovec vector{bytes.data(), bytes.size()};
    msghdr message{};
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    alignas(cmsghdr) std::array<char, CMSG_SPACE(sizeof(int))> control{};
    if (descriptor >= 0) {
        message.msg_control = control.data();
        message.msg_controllen = control.size();
        cmsghdr* header = CMSG_FIRSTHDR(&message);
        header->cmsg_level = SOL_SOCKET;
        header->cmsg_type = SCM_RIGHTS;
        header->cmsg_len = CMSG_LEN(sizeof(int));
        std::memcpy(CMSG_DATA(header), &descriptor, sizeof descriptor);
    }
    if (::sendmsg(connection_.get(), &message, MSG_NOSIGNAL) < 0) {
        std::fprintf(stderr, "export: an answer was not sent: %s\n", std::strerror(errno));
    }
}

}  // namespace rwin::integration
