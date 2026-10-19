#include "host/export/unique_fd.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/parent_control.hpp"
#include "host/link/guest_window.hpp"
#include "proto/feature.hpp"
#include "proto/hello.hpp"
#include "proto/message_writer.hpp"
#include "proto/sync_begin.hpp"
#include "proto/window_fields.hpp"
#include "proto/window_id.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QRect>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <utility>
#include <unistd.h>

namespace {
int failures = 0;

void check(bool ok, const char* label) {
    if (ok)
        return;
    std::printf("FAIL: %s\n", label);
    ++failures;
}

bool wait_for(const std::function<bool()>& done) {
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (done())
            loop.quit();
    });
    poll.start(5);
    QTimer::singleShot(1000, &loop, &QEventLoop::quit);
    if (!done())
        loop.exec();
    return done();
}

void parent_control(QCoreApplication& app) {
    int commands[2];
    int statuses[2];
    if (::pipe2(commands, O_NONBLOCK | O_CLOEXEC) != 0 ||
        ::pipe2(statuses, O_NONBLOCK | O_CLOEXEC) != 0) {
        check(false, "pipes available");
        return;
    }
    rwin::host::UniqueFd command_read(commands[0]);
    rwin::host::UniqueFd command_write(commands[1]);
    rwin::host::UniqueFd status_read(statuses[0]);
    rwin::host::UniqueFd status_write(statuses[1]);
    rwin::host::ParentControl control(command_read.get(), status_write.get());
    int raised = 0;
    int closed = 0;
    int errors = 0;
    QObject::connect(&control, &rwin::host::ParentControl::raise_requested, &app,
                     [&] { ++raised; });
    QObject::connect(&control, &rwin::host::ParentControl::closed, &app, [&] { ++closed; });
    QObject::connect(&control, &rwin::host::ParentControl::failed, &app, [&] { ++errors; });
    control.report_listening(43210);
    control.report_ready();
    control.report_app(QStringLiteral("Vendor.App"));
    char bytes[128];
    const auto count = ::read(status_read.get(), bytes, sizeof(bytes));
    check(count > 0 && QByteArray(bytes, count) == "LISTENING 43210\nREADY\nAPP Vendor.App\n",
          "status records are flushed to parent");
    check(::write(command_write.get(), "RA", 2) == 2, "write partial command");
    QCoreApplication::processEvents();
    check(raised == 0, "partial command is not executed");
    check(::write(command_write.get(), "ISE\nRAISE\n", 10) == 10,
          "finish command and batch another");
    check(wait_for([&] { return raised == 2; }), "fragmented and batched raise requests work");
    command_write = rwin::host::UniqueFd{};
    check(wait_for([&] { return closed == 1; }), "parent death is detected through EOF");
    QCoreApplication::processEvents();
    check(closed == 1 && errors == 0, "EOF is reported once, not spun on");
}

// A guest half: dials the link, and speaks to it as the guest would.
class FakeGuest {
public:
    explicit FakeGuest(std::uint16_t port) {
        socket_.connectToHost(QHostAddress::LocalHost, port);
        check(socket_.waitForConnected(1000), "guest can dial loopback");
    }

    void send(rwin::MessageWriter message) {
        const auto bytes = message.take();
        check(socket_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()))
                  == static_cast<qint64>(bytes.size()),
              "write guest message");
        socket_.flush();
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

    void disconnect() { socket_.disconnectFromHost(); }

private:
    QTcpSocket socket_;
};

rwin::WindowFields titled(const char* title) {
    rwin::WindowFields fields;
    fields.title = title;
    fields.show = rwin::WindowFields::Show::Shown;
    fields.geometry.frame = {10, 20, 300, 200};
    fields.geometry.picture = {0, 0, 300, 200};
    return fields;
}

void a_guest_without_windows_is_refused(QCoreApplication& app) {
    rwin::host::GuestLink link;
    check(link.listen(0, QHostAddress::LocalHost), "listens");
    int mismatched = 0;
    int connected = 0;
    QObject::connect(&link, &rwin::host::GuestLink::mismatched, &app, [&] { ++mismatched; });
    QObject::connect(&link, &rwin::host::GuestLink::connected, &app, [&] { ++connected; });
    FakeGuest guest{link.port()};
    guest.hello(rwin::Hello{"elsewhere", {rwin::Feature{"input", 1}}});
    check(wait_for([&] { return mismatched == 1; }) && connected == 0,
          "a guest that does not speak windows is refused");

    rwin::host::GuestLink older;
    check(older.listen(0, QHostAddress::LocalHost), "listens");
    int refused = 0;
    QObject::connect(&older, &rwin::host::GuestLink::mismatched, &app, [&] { ++refused; });
    FakeGuest first{older.port()};
    first.bare(rwin::MessageId::SyncEnd);
    check(wait_for([&] { return refused == 1; }), "a first message that is not a hello is refused");
}

void guest_link(QCoreApplication& app) {
    rwin::host::GuestLink link;
    check(link.listen(0, QHostAddress::LocalHost) && link.port() != 0, "OS allocates the actual listening port");
    int connected = 0;
    int arrived = 0;
    int changed = 0;
    int gone = 0;
    int synced = 0;
    int disconnected = 0;
    int frames = 0;
    QObject::connect(&link, &rwin::host::GuestLink::connected, &app, [&] { ++connected; });
    QObject::connect(&link, &rwin::host::GuestLink::disconnected, &app, [&] { ++disconnected; });
    QObject::connect(&link, &rwin::host::GuestLink::window_new, &app, [&](rwin::WindowId) { ++arrived; });
    QObject::connect(&link, &rwin::host::GuestLink::window_changed, &app, [&](rwin::WindowId, quint32) { ++changed; });
    QObject::connect(&link, &rwin::host::GuestLink::window_gone, &app, [&](rwin::WindowId) { ++gone; });
    QObject::connect(&link, &rwin::host::GuestLink::sync_ended, &app, [&] { ++synced; });
    QObject::connect(&link, &rwin::host::GuestLink::frame_arrived, &app, [&](rwin::WindowId window, const QImage& pixels) {
        ++frames;
        check(window == rwin::WindowId{0x30, 1} && !pixels.isNull() && pixels.size() == QSize(1, 1),
              "only a valid frame is emitted, for its window");
    });
    FakeGuest guest{link.port()};
    guest.hello(rwin::Hello::ours());
    check(wait_for([&] { return connected == 1; }), "handshake completes");
    check(link.features().has(rwin::Feature::kWindows), "the features both speak are active");

    rwin::MessageWriter begin(rwin::MessageId::SyncBegin);
    rwin::SyncBegin{rwin::SyncBegin::Reason::Connect}.write(begin);
    guest.send(std::move(begin));
    guest.window_new(rwin::WindowId{0x30, 1}, titled("first"));
    guest.window_new(rwin::WindowId{0x40, 2}, titled("second"));
    guest.bare(rwin::MessageId::SyncEnd);
    check(wait_for([&] { return synced == 1; }) && arrived == 2, "the windows said in a sync arrive");

    rwin::MessageWriter retitle(rwin::MessageId::WindowChanged);
    rwin::WindowId{0x30, 1}.write(retitle);
    titled("renamed").write(retitle, rwin::WindowFields::kTitle);
    guest.send(std::move(retitle));
    rwin::MessageWriter stranger(rwin::MessageId::WindowChanged);
    rwin::WindowId{0x99, 9}.write(stranger);
    titled("nobody").write(stranger, rwin::WindowFields::kTitle);
    guest.send(std::move(stranger));
    check(wait_for([&] { return changed == 1; }), "a change arrives");
    const rwin::host::GuestWindow* first = link.windows().find(rwin::WindowId{0x30, 1});
    check(first != nullptr && first->title() == QStringLiteral("renamed") && first->frame() == QRect(10, 20, 300, 200),
          "read over what was known: the title changed, the frame kept");
    check(link.windows().find(rwin::WindowId{0x99, 9}) == nullptr && changed == 1,
          "a change to a window not known is ignored");

    rwin::MessageWriter again(rwin::MessageId::SyncBegin);
    rwin::SyncBegin{rwin::SyncBegin::Reason::Resync}.write(again);
    guest.send(std::move(again));
    guest.window_new(rwin::WindowId{0x30, 1}, titled("renamed"));
    guest.bare(rwin::MessageId::SyncEnd);
    check(wait_for([&] { return synced == 2; }) && gone == 1 && arrived == 2,
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
    check(wait_for([&] { return frames == 1; }), "overflowing frame dimensions do not create false readiness");
    guest.disconnect();
    check(wait_for([&] { return disconnected == 1; }), "guest disconnect is detected");
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    parent_control(app);
    a_guest_without_windows_is_refused(app);
    guest_link(app);
    return failures == 0 ? 0 : 1;
}
