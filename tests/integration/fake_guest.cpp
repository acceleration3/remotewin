#include "fake_guest.hpp"

#include "fake_export_device.hpp"

#include "host/latency/frame_stamps.hpp"
#include "proto/feature.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/frame_route.hpp"
#include "proto/hello.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/min_max.hpp"
#include "proto/move_size_end.hpp"
#include "proto/move_size_start.hpp"
#include "proto/pointer_button.hpp"
#include "proto/slot_ring.hpp"
#include "proto/sync_begin.hpp"
#include "proto/sys_param.hpp"
#include "proto/window_move.hpp"
#include "proto/z_order.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace rwin::integration {

namespace {

constexpr std::size_t kChunk = 256 * 1024;
constexpr std::uint32_t kPictureColour = 0xFF3060A0;
// What Windows' capture hands over of a window as it is minimised: its
// minimised stub (measured on build 26200: 185x27 for Notepad).
constexpr Rect kMinimizedStub{0, 0, 185, 27};

// What this guest speaks: everything a pixel-path guest does.
Hello hello() {
    Hello ours;
    ours.build = "remotewin integration guest";
    for (const std::string_view name : {Feature::kWindows, Feature::kInput, Feature::kFramesPixels,
                                        Feature::kFramesShared, Feature::kMoveSize, Feature::kAppId}) {
        ours.features.push_back(Feature{std::string(name), 1});
    }
    return ours;
}

}  // namespace

FakeGuest::FakeGuest(host::EventLoop& loop, ModelWindow& window, Timing timing, FakeExportDevice* device)
    : loop_(loop), window_(window), timing_(timing), device_(device), chunk_(kChunk) {
    carry_ = loop_.timer([this] { carry_out(); });
    capture_ = loop_.timer([this] { send_frame(); });
}

FakeGuest::~FakeGuest() {
    watch_.reset();
    if (fd_ >= 0) ::close(fd_);
}

bool FakeGuest::dial(std::uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "guest: cannot dial port %u: %s\n", port, std::strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    const int no_delay = 1;
    static_cast<void>(::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &no_delay, sizeof(no_delay)));
    watch_ = loop_.watch(fd_, POLLIN, [this](short) { read_host(); });
    MessageWriter greeting{MessageId::GuestHello};
    hello().write(greeting);
    send(greeting.take());
    MessageWriter begin{MessageId::SyncBegin};
    SyncBegin{SyncBegin::Reason::Connect}.write(begin);
    send(begin.take());
    said_ = window_.fields();
    MessageWriter made{MessageId::WindowNew};
    id_.write(made);
    said_.write(made, WindowFields::kAll);
    send(made.take());
    send(MessageWriter{MessageId::SyncEnd}.take());
    MessageWriter order{MessageId::ZOrder};
    ZOrder{WindowId{}, {id_}}.write(order);
    send(order.take());
    for (const SysParam::Param param : {SysParam::Param::WorkArea, SysParam::Param::Monitor}) {
        MessageWriter setting{MessageId::GuestSysParam};
        SysParam{param, window_.work_area(), false}.write(setting);
        send(setting.take());
    }
    return true;
}

void FakeGuest::act(SysCommand::Command command) {
    stale_frame_ = window_.minimized() && command == SysCommand::Command::Restore;
    window_.command(command);
    report();
}

void FakeGuest::place_itself(const Rect& frame) {
    window_.place(frame);
    report();
}

void FakeGuest::read_host() {
    const ssize_t got = ::read(fd_, chunk_.data(), chunk_.size());
    if (got <= 0) {
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) return;
        watch_.reset();
        ::close(fd_);
        fd_ = -1;
        return;
    }
    decoder_.feed(chunk_.data(), static_cast<std::size_t>(got));
    const std::uint8_t* payload = nullptr;
    std::size_t size = 0;
    while (decoder_.next(&payload, &size)) {
        MessageReader message{payload, size};
        take(message);
    }
}

void FakeGuest::take(MessageReader& message) {
    switch (message.id()) {
        case MessageId::Subscribe: {
            const std::optional<WindowId> id = WindowId::read(message);
            if (Other* played = id.has_value() ? other(*id) : nullptr) {
                played->subscribed = true;
                picture_other(*played);
                return;
            }
            subscribed_ = true;
            capture_->start(timing_.frame, false);
            return;
        }
        case MessageId::FrameRoute:
            if (const std::optional<FrameRoute> route = FrameRoute::read(message)) {
                shared_ = route->shared_surfaces && device_ != nullptr;
            }
            return;
        case MessageId::WindowPixels:
            if (const std::optional<WindowId> id = WindowId::read(message)) {
                if (Other* played = other(*id)) {
                    played->pixels_only = true;
                    picture_other(*played);
                }
            }
            return;
        case MessageId::Activate:
            if (const std::optional<Activate> activation = Activate::read(message)) {
                activations_.push_back(*activation);
            }
            return;
        case MessageId::WindowMove:
            if (const std::optional<WindowMove> move = WindowMove::read(message)) {
                asked_.push_back(Asked{host::now_ns(), move->frame, move->window});
                if (move->window == id_) ask(move->frame);
            }
            return;
        case MessageId::SysCommand:
            if (const std::optional<SysCommand> command = SysCommand::read(message)) {
                asked_.push_back(Asked{host::now_ns(), command->command, command->window});
                if (command->window == id_) ask(command->command);
            }
            return;
        case MessageId::PointerButton: {
            const std::optional<PointerButton> button = PointerButton::read(message);
            if (!button.has_value() || button->button != PointerButton::Button::Left) return;
            if (!button->pressed) {
                if (!looping_.has_value()) return;
                MessageWriter out{MessageId::MoveSizeEnd};
                MoveSizeEnd{id_, *looping_, window_.frame()}.write(out);
                send(out.take());
                looping_.reset();
                ++loops_ended_;
                return;
            }
            if (const std::optional<MoveSizeKind> kind = window_.loop_at(button->x, button->y)) ask(*kind);
            return;
        }
        default:
            return;
    }
}

void FakeGuest::ask(Request request) {
    due_.push_back(Due{std::chrono::steady_clock::now() + timing_.answer, request});
    if (!carry_->active()) carry_->start(timing_.answer, false);
}

void FakeGuest::carry_out() {
    const auto now = std::chrono::steady_clock::now();
    while (!due_.empty() && due_.front().at <= now) {
        const Request request = due_.front().request;
        due_.pop_front();
        apply(request);
    }
    if (due_.empty()) return;
    carry_->start(std::chrono::ceil<std::chrono::milliseconds>(due_.front().at - now), false);
}

void FakeGuest::apply(const Request& request) {
    if (const auto* frame = std::get_if<Rect>(&request)) {
        window_.place(*frame);
    } else if (const auto* command = std::get_if<SysCommand::Command>(&request)) {
        stale_frame_ = window_.minimized() && *command == SysCommand::Command::Restore;
        window_.command(*command);
    } else if (const auto* kind = std::get_if<MoveSizeKind>(&request)) {
        looping_ = *kind;
        ++loops_begun_;
        const Rect frame = window_.frame();
        MessageWriter limits{MessageId::MinMax};
        MinMax{id_, 0, 0, 0, 0, 0, 0, 0, 0}.write(limits);
        send(limits.take());
        MessageWriter start{MessageId::MoveSizeStart};
        MoveSizeStart{id_, *kind, frame.x, frame.y}.write(start);
        send(start.take());
        return;
    }
    report();
}

// The fields that changed, then a frame of the size it now is: Windows
// says the window moved before DWM composes it there.
void FakeGuest::report() {
    const WindowFields now = window_.fields();
    if (const std::uint32_t mask = now.differences(said_); mask != 0) {
        MessageWriter out{MessageId::WindowChanged};
        id_.write(out);
        now.write(out, mask);
        send(out.take());
        if ((mask & WindowFields::kGeometry) != 0) placed_.push_back(Placed{host::now_ns(), now.geometry.frame});
        said_ = now;
    }
    if (subscribed_ && !capture_->active()) capture_->start(timing_.frame, false);
}

void FakeGuest::send_frame() {
    if (window_.minimized() || fd_ < 0) return;
    if (std::exchange(stale_frame_, false)) {
        send_picture(kMinimizedStub.width, kMinimizedStub.height);
        capture_->start(timing_.frame, false);
        return;
    }
    send_picture(window_.frame().width, window_.frame().height);
}

void FakeGuest::send_picture(std::uint32_t width, std::uint32_t height) {
    send_picture_of(id_, width, height);
    pictured_.push_back(Pictured{host::now_ns(), width, height});
}

void FakeGuest::send_picture_of(WindowId id, std::uint32_t width, std::uint32_t height) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (pixels_.size() != count * 4) {
        pixels_.resize(count * 4);
        for (std::size_t at = 0; at < count; ++at) std::memcpy(pixels_.data() + at * 4, &kPictureColour, 4);
    }
    MessageWriter out{MessageId::Frame};
    id.write(out);
    out.put_u32(width);
    out.put_u32(height);
    out.put_bytes(pixels_.data(), pixels_.size());
    send(out.take());
}

FakeGuest::Other* FakeGuest::other(WindowId id) {
    const auto found = std::find_if(others_.begin(), others_.end(), [id](const Other& o) { return o.id == id; });
    return found == others_.end() ? nullptr : &*found;
}

bool FakeGuest::subscribed_to(WindowId id) const {
    const auto found = std::find_if(others_.begin(), others_.end(), [id](const Other& o) { return o.id == id; });
    return found != others_.end() && found->subscribed;
}

// Windows' capture has nothing of a hidden window, or of one of no size.
void FakeGuest::picture_other(Other& played) {
    const Rect& picture = played.said.geometry.picture;
    if (!played.subscribed || played.said.show == WindowFields::Show::Hidden || picture.width == 0
        || picture.height == 0) {
        return;
    }
    if (shared_ && !played.pixels_only) {
        copy_into_ring(played, picture.width, picture.height);
        return;
    }
    send_picture_of(played.id, picture.width, picture.height);
}

// A ring is made the first time, or again when the picture outgrows it.
void FakeGuest::copy_into_ring(Other& played, std::uint32_t width, std::uint32_t height) {
    if (played.ring == 0 || width > played.ring_width || height > played.ring_height) {
        SlotRing ring{played.id, ++rings_, width, height, {}};
        for (std::uint64_t& surface : ring.surfaces) {
            surface = ++surfaces_;
            device_->made(surface, width, height);
        }
        played.slot_surface = ring.surfaces[0];
        played.copies = 0;
        played.ring = ring.ring;
        played.ring_width = width;
        played.ring_height = height;
        ++played.rings_made;
        MessageWriter made{MessageId::SlotRing};
        ring.write(made);
        send(made.take());
    }
    // Slot 0, every time: the host frees it before the next.
    device_->copied(played.slot_surface);
    MessageWriter out{MessageId::FrameInSlot};
    FrameInSlot{played.id, played.ring, 0, width, height, ++played.copies, 0, 0, 0}.write(out);
    send(out.take());
}

std::size_t FakeGuest::rings_made(WindowId id) const {
    const auto found = std::find_if(others_.begin(), others_.end(), [id](const Other& o) { return o.id == id; });
    return found == others_.end() ? 0 : found->rings_made;
}

bool FakeGuest::sent_back_to_pixels(WindowId id) const {
    const auto found = std::find_if(others_.begin(), others_.end(), [id](const Other& o) { return o.id == id; });
    return found != others_.end() && found->pixels_only;
}

void FakeGuest::show_window(WindowId id, const WindowFields& fields) {
    others_.push_back(Other{id, fields, false});
    MessageWriter made{MessageId::WindowNew};
    id.write(made);
    fields.write(made, WindowFields::kAll);
    send(made.take());
}

void FakeGuest::change_window(WindowId id, const WindowFields& fields) {
    Other* played = other(id);
    if (played == nullptr) return;
    if (const std::uint32_t mask = fields.differences(played->said); mask != 0) {
        MessageWriter out{MessageId::WindowChanged};
        id.write(out);
        fields.write(out, mask);
        send(out.take());
        played->said = fields;
    }
    picture_other(*played);
}

void FakeGuest::forget_window(WindowId id) {
    std::erase_if(others_, [id](const Other& o) { return o.id == id; });
    MessageWriter out{MessageId::WindowGone};
    id.write(out);
    send(out.take());
}

void FakeGuest::stack(const ZOrder& order) {
    MessageWriter out{MessageId::ZOrder};
    order.write(out);
    send(out.take());
}

void FakeGuest::send(const std::vector<std::uint8_t>& bytes) {
    std::size_t sent = 0;
    while (fd_ >= 0 && sent < bytes.size()) {
        const ssize_t wrote = ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (wrote < 0) {
            if (errno == EINTR) continue;
            std::fprintf(stderr, "guest: the host stopped reading: %s\n", std::strerror(errno));
            return;
        }
        sent += static_cast<std::size_t>(wrote);
    }
}

}  // namespace rwin::integration
