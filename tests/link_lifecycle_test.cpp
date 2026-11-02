#include "host/export/unique_fd.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "host/link/parent_control.hpp"
#include "host/loop/poll_loop.hpp"
#include "proto/feature.hpp"
#include "proto/hello.hpp"
#include "proto/message_writer.hpp"
#include "proto/sync_begin.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>

namespace {

int failures = 0;

void check(bool ok, const char* label) {
    if (ok) return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

using rwin::host::PollLoop;

bool wait_for(PollLoop& loop, const std::function<bool()>& done) {
    if (done()) return true;
    const auto poll = loop.timer([&] {
        if (done()) loop.quit(0);
    });
    const auto give_up = loop.timer([&] { loop.quit(0); });
    poll->start(std::chrono::milliseconds(5), true);
    give_up->start(std::chrono::milliseconds(1000), false);
    static_cast<void>(loop.run());
    return done();
}

void parent_control(PollLoop& loop) {
    int commands[2];
    int statuses[2];
    if (::pipe2(commands, O_NONBLOCK | O_CLOEXEC) != 0 || ::pipe2(statuses, O_NONBLOCK | O_CLOEXEC) != 0) {
        check(false, "pipes available");
        return;
    }
    rwin::host::UniqueFd command_read(commands[0]);
    rwin::host::UniqueFd command_write(commands[1]);
    rwin::host::UniqueFd status_read(statuses[0]);
    rwin::host::UniqueFd status_write(statuses[1]);
    rwin::host::ParentControl control(loop, command_read.get(), status_write.get());
    int raised = 0;
    int closed = 0;
    int errors = 0;
    std::string token;
    control.raise_requested.connect([&](const std::string& handed) {
        ++raised;
        token = handed;
    });
    control.closed.connect([&] { ++closed; });
    control.failed.connect([&] { ++errors; });
    control.report_listening(43210);
    control.report_ready();
    control.report_app("Vendor.App");
    char bytes[128];
    const auto count = ::read(status_read.get(), bytes, sizeof(bytes));
    check(count > 0 && std::string(bytes, static_cast<std::size_t>(count)) == "LISTENING 43210\nREADY\nAPP Vendor.App\n",
          "status records are flushed to parent");
    check(::write(command_write.get(), "RA", 2) == 2, "write partial command");
    static_cast<void>(wait_for(loop, [&] { return raised > 0; }));
    check(raised == 0, "partial command is not executed");
    check(::write(command_write.get(), "ISE\nRAISE tok-1\n", 16) == 16, "finish command and batch another");
    check(wait_for(loop, [&] { return raised == 2; }), "fragmented and batched raise requests work");
    check(token == "tok-1", "a raise carries the activation token it was handed");
    command_write = rwin::host::UniqueFd{};
    check(wait_for(loop, [&] { return closed == 1; }), "parent death is detected through EOF");
    static_cast<void>(wait_for(loop, [] { return false; }));
    check(closed == 1 && errors == 0, "EOF is reported once, not spun on");
}

class FakeGuest {
public:
    explicit FakeGuest(std::uint16_t port) : socket_(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(::connect(socket_.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
              "guest can dial loopback");
    }

    void send(rwin::MessageWriter message) {
        const auto bytes = message.take();
        check(::write(socket_.get(), bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size()),
              "write guest message");
    }

    void hello(const rwin::Hello& hello) {
        rwin::MessageWriter message(rwin::MessageId::GuestHello);
        hello.write(message);
        send(std::move(message));
    }

    void window_new(rwin::WindowId id, const rwin::WindowFields& fields) {
        rwin::MessageWriter message(rwin::MessageId::WindowNew);
        id.write(message);
        fields.write(message, rwin::WindowFields::kAll);
        send(std::move(message));
    }

    void bare(rwin::MessageId id) { send(rwin::MessageWriter(id)); }

    void disconnect() { socket_ = rwin::host::UniqueFd{}; }

private:
    rwin::host::UniqueFd socket_;
};

rwin::WindowFields titled(const char* title) {
    rwin::WindowFields fields;
    fields.title = title;
    fields.show = rwin::WindowFields::Show::Shown;
    fields.geometry.frame = {10, 20, 300, 200};
    fields.geometry.picture = {0, 0, 300, 200};
    return fields;
}

void a_guest_without_windows_is_refused(PollLoop& loop) {
    rwin::host::GuestLink link{loop};
    check(link.listen(0, true), "listens");
    int mismatched = 0;
    int connected = 0;
    link.mismatched.connect([&](const std::string&) { ++mismatched; });
    link.connected.connect([&] { ++connected; });
    FakeGuest guest{link.port()};
    guest.hello(rwin::Hello{"elsewhere", {rwin::Feature{"input", 1}}});
    check(wait_for(loop, [&] { return mismatched == 1; }) && connected == 0,
          "a guest that does not speak windows is refused");

    rwin::host::GuestLink older{loop};
    check(older.listen(0, true), "listens");
    int refused = 0;
    older.mismatched.connect([&](const std::string&) { ++refused; });
    FakeGuest first{older.port()};
    first.bare(rwin::MessageId::SyncEnd);
    check(wait_for(loop, [&] { return refused == 1; }), "a first message that is not a hello is refused");
}

void guest_link(PollLoop& loop) {
    rwin::host::GuestLink link{loop};
    check(link.listen(0, true) && link.port() != 0, "the OS allocates the actual listening port");
    int connected = 0;
    int arrived = 0;
    int changed = 0;
    int gone = 0;
    int synced = 0;
    int disconnected = 0;
    int frames = 0;
    link.connected.connect([&] { ++connected; });
    link.disconnected.connect([&] { ++disconnected; });
    link.window_new.connect([&](rwin::WindowId) { ++arrived; });
    link.window_changed.connect([&](rwin::WindowId, std::uint32_t) { ++changed; });
    link.window_gone.connect([&](rwin::WindowId) { ++gone; });
    link.sync_ended.connect([&] { ++synced; });
    link.frame_arrived.connect([&](rwin::WindowId window, const rwin::host::PixelsView& pixels) {
        ++frames;
        check(window == rwin::WindowId{0x30, 1} && pixels.bgra != nullptr && pixels.width == 1 && pixels.height == 1
                  && pixels.stride == 4,
              "only a valid frame is told, for its window, as it lies in the message");
    });
    FakeGuest guest{link.port()};
    guest.hello(rwin::Hello::ours());
    check(wait_for(loop, [&] { return connected == 1; }), "handshake completes");
    check(link.features().has(rwin::Feature::kWindows), "the features both speak are active");

    rwin::MessageWriter begin(rwin::MessageId::SyncBegin);
    rwin::SyncBegin{rwin::SyncBegin::Reason::Connect}.write(begin);
    guest.send(std::move(begin));
    guest.window_new(rwin::WindowId{0x30, 1}, titled("first"));
    guest.window_new(rwin::WindowId{0x40, 2}, titled("second"));
    guest.bare(rwin::MessageId::SyncEnd);
    check(wait_for(loop, [&] { return synced == 1; }) && arrived == 2, "the windows said in a sync arrive");

    rwin::MessageWriter retitle(rwin::MessageId::WindowChanged);
    rwin::WindowId{0x30, 1}.write(retitle);
    titled("renamed").write(retitle, rwin::WindowFields::kTitle);
    guest.send(std::move(retitle));
    rwin::MessageWriter stranger(rwin::MessageId::WindowChanged);
    rwin::WindowId{0x99, 9}.write(stranger);
    titled("nobody").write(stranger, rwin::WindowFields::kTitle);
    guest.send(std::move(stranger));
    check(wait_for(loop, [&] { return changed == 1; }), "a change arrives");
    const rwin::host::GuestWindow* first = link.windows().find(rwin::WindowId{0x30, 1});
    check(first != nullptr && first->title() == "renamed" && first->frame() == rwin::host::Box{10, 20, 300, 200},
          "read over what was known: the title changed, the frame kept");
    check(link.windows().find(rwin::WindowId{0x99, 9}) == nullptr && changed == 1,
          "a change to a window not known is ignored");

    rwin::MessageWriter again(rwin::MessageId::SyncBegin);
    rwin::SyncBegin{rwin::SyncBegin::Reason::Resync}.write(again);
    guest.send(std::move(again));
    guest.window_new(rwin::WindowId{0x30, 1}, titled("renamed"));
    guest.bare(rwin::MessageId::SyncEnd);
    check(wait_for(loop, [&] { return synced == 2; }) && gone == 1 && arrived == 2,
          "a resync that does not say a window again says it gone");

    rwin::MessageWriter huge_frame(rwin::MessageId::Frame);
    rwin::WindowId{0x30, 1}.write(huge_frame);
    huge_frame.put_u32(0x80000000);
    huge_frame.put_u32(2);
    guest.send(std::move(huge_frame));
    rwin::MessageWriter good_frame(rwin::MessageId::Frame);
    rwin::WindowId{0x30, 1}.write(good_frame);
    good_frame.put_u32(1);
    good_frame.put_u32(1);
    good_frame.put_u32(0xffffffff);
    guest.send(std::move(good_frame));
    check(wait_for(loop, [&] { return frames == 1; }), "overflowing frame dimensions do not create false readiness");
    guest.disconnect();
    check(wait_for(loop, [&] { return disconnected == 1; }), "guest disconnect is detected");
}

}

int main() {
    PollLoop loop;
    parent_control(loop);
    a_guest_without_windows_is_refused(loop);
    guest_link(loop);
    if (failures != 0) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("link lifecycle: all checks passed\n");
    return 0;
}
