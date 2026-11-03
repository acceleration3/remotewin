#include "host/input/qmp_input.hpp"

#include "host/input/qnum_code.hpp"
#include "logging/logger.hpp"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

namespace rwin::host {

namespace {

constexpr std::chrono::milliseconds kFirstRetry{250};
constexpr std::chrono::milliseconds kLastRetry{5000};
constexpr std::int32_t kNotch = 120;
constexpr int kAbsoluteRange = 32768;
constexpr std::size_t kMaxUnsent = 1 << 20;

constexpr const char* kCapabilities = R"({"execute":"qmp_capabilities"})";

const char* button_name(std::uint32_t evdev_code) noexcept {
    switch (evdev_code) {
        case BTN_LEFT: return "left";
        case BTN_MIDDLE: return "middle";
        case BTN_RIGHT: return "right";
        case BTN_SIDE:
        case BTN_BACK: return "side";
        case BTN_EXTRA:
        case BTN_FORWARD: return "extra";
        default: return nullptr;
    }
}

int absolute_of(int position, int extent) noexcept {
    return std::clamp(static_cast<int>((static_cast<long long>(position) * 2 + 1) * kAbsoluteRange / (extent * 2LL)), 0,
                      kAbsoluteRange - 1);
}

}

QmpInput::QmpInput(EventLoop& loop, std::string path)
    : loop_(loop), path_(std::move(path)), reconnect_(loop.timer([this] { connect(); })),
      batch_(loop.timer([this] { flush(); })), backoff_(kFirstRetry) {
    connect();
}

QmpInput::~QmpInput() = default;

void QmpInput::connect() {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path_.empty() || path_.size() >= sizeof address.sun_path) {
        RWIN_ERROR("qmp", "'{}' is no socket path", path_);
        return;
    }
    std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);
    UniqueFd socket{::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0)};
    if (!socket.valid() || ::connect(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0) {
        if (!reported_unavailable_) RWIN_WARN("qmp", "input monitor '{}' is not available: {}", path_, std::strerror(errno));
        reported_unavailable_ = true;
        retry_later();
        return;
    }
    socket_ = std::move(socket);
    state_ = State::Greeting;
    incoming_.clear();
    outgoing_.clear();
    watch_ = loop_.watch(socket_.get(), POLLIN, [this](short events) { on_ready(events); });
}

void QmpInput::retry_later() {
    reconnect_->start(backoff_, false);
    backoff_ = std::min(backoff_ * 2, kLastRetry);
}

void QmpInput::drop(const char* why) {
    if (state_ == State::Ready) RWIN_WARN("qmp", "input monitor '{}' lost ({}): input goes over the wire", path_, why);
    state_ = State::Closed;
    watch_.reset();
    socket_ = UniqueFd{};
    events_.clear();
    batch_->stop();
    held_.clear();
    reported_unavailable_ = true;
    retry_later();
}

void QmpInput::on_ready(short events) {
    if ((events & POLLIN) != 0) read_replies();
    if (state_ == State::Closed) return;
    if ((events & POLLOUT) != 0) write_pending();
    if (state_ == State::Closed) return;
    if ((events & (POLLERR | POLLHUP)) != 0 && (events & POLLIN) == 0) drop("hung up");
}

void QmpInput::read_replies() {
    std::array<char, 4096> chunk{};
    for (;;) {
        const ssize_t got = ::recv(socket_.get(), chunk.data(), chunk.size(), 0);
        if (got == 0) {
            drop("closed");
            return;
        }
        if (got < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) drop(std::strerror(errno));
            return;
        }
        incoming_.append(chunk.data(), static_cast<std::size_t>(got));
        for (std::size_t end; (end = incoming_.find('\n')) != std::string::npos;) {
            const std::string line = incoming_.substr(0, end);
            incoming_.erase(0, end + 1);
            take_line(line);
            if (state_ == State::Closed) return;
        }
    }
}

void QmpInput::take_line(const std::string& line) {
    if (state_ == State::Greeting && line.starts_with("{\"QMP\"")) {
        state_ = State::Negotiating;
        send_line(kCapabilities);
        return;
    }
    if (state_ != State::Negotiating) return;
    if (line.find("\"return\"") != std::string::npos) {
        state_ = State::Ready;
        backoff_ = kFirstRetry;
        reported_unavailable_ = false;
        RWIN_INFO("qmp", "input goes through QEMU's virtual devices ('{}')", path_);
    } else if (line.find("\"error\"") != std::string::npos) {
        RWIN_ERROR("qmp", "'{}' refused the handshake: {}", path_, line);
        drop("handshake refused");
    }
}

void QmpInput::send_line(const std::string& line) {
    outgoing_ += line;
    outgoing_ += '\n';
    write_pending();
}

void QmpInput::write_pending() {
    while (!outgoing_.empty()) {
        const ssize_t sent = ::send(socket_.get(), outgoing_.data(), outgoing_.size(), MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (sent < 0) {
            drop(std::strerror(errno));
            return;
        }
        outgoing_.erase(0, static_cast<std::size_t>(sent));
    }
    if (outgoing_.size() > kMaxUnsent) {
        drop("not reading");
        return;
    }
    want_events();
}

void QmpInput::want_events() {
    if (watch_) watch_->want(outgoing_.empty() ? POLLIN : POLLIN | POLLOUT);
}

void QmpInput::queue(std::string event) {
    if (state_ != State::Ready) return;
    events_.push_back(std::move(event));
    batch_->start(std::chrono::milliseconds::zero(), false);
}

void QmpInput::flush() {
    if (state_ != State::Ready || events_.empty()) return;
    std::string list;
    for (const std::string& event : events_) {
        if (!list.empty()) list += ',';
        list += event;
    }
    events_.clear();
    last_is_motion_ = false;
    send_line(std::format(R"({{"execute":"input-send-event","arguments":{{"events":[{}]}}}})", list));
}

void QmpInput::move_to(int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    const int ax = absolute_of(x, width);
    const int ay = absolute_of(y, height);
    const std::string pair = std::format(R"({{"type":"abs","data":{{"axis":"x","value":{}}}}},{{"type":"abs","data":{{"axis":"y","value":{}}}}})",
                                         ax, ay);
    if (last_is_motion_ && !events_.empty()) {
        events_.back() = pair;
        return;
    }
    queue(pair);
    last_is_motion_ = true;
}

bool QmpInput::button(std::uint32_t evdev_code, bool pressed) {
    const char* name = button_name(evdev_code);
    if (name == nullptr || state_ != State::Ready) return false;
    last_is_motion_ = false;
    queue(std::format(R"({{"type":"btn","data":{{"down":{},"button":"{}"}}}})", pressed, name));
    return true;
}

void QmpInput::notch(const char* button) {
    last_is_motion_ = false;
    queue(std::format(R"({{"type":"btn","data":{{"down":true,"button":"{}"}}}})", button));
    queue(std::format(R"({{"type":"btn","data":{{"down":false,"button":"{}"}}}})", button));
}

void QmpInput::wheel(std::int32_t dx120, std::int32_t dy120) {
    owed_x_ += dx120;
    owed_y_ += dy120;
    for (; owed_y_ >= kNotch; owed_y_ -= kNotch) notch("wheel-down");
    for (; owed_y_ <= -kNotch; owed_y_ += kNotch) notch("wheel-up");
    for (; owed_x_ >= kNotch; owed_x_ -= kNotch) notch("wheel-right");
    for (; owed_x_ <= -kNotch; owed_x_ += kNotch) notch("wheel-left");
}

bool QmpInput::key(std::uint32_t evdev_code, bool pressed) {
    const std::uint16_t qnum = qnum_code(evdev_code);
    if (qnum == 0 || state_ != State::Ready) return false;
    if (pressed) {
        held_.insert(qnum);
    } else {
        held_.erase(qnum);
    }
    last_is_motion_ = false;
    queue(std::format(R"({{"type":"key","data":{{"down":{},"key":{{"type":"number","data":{}}}}}}})", pressed, qnum));
    return true;
}

void QmpInput::release_keys() {
    for (const std::uint16_t qnum : held_) {
        queue(std::format(R"({{"type":"key","data":{{"down":false,"key":{{"type":"number","data":{}}}}}}})", qnum));
    }
    held_.clear();
}

}
