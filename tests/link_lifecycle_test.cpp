#include "host/export/unique_fd.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/parent_control.hpp"
#include "proto/guest_desktop.hpp"
#include "proto/message_writer.hpp"
#include "proto/wire_hello.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>
#include <fcntl.h>
#include <functional>
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
    char bytes[128];
    const auto count = ::read(status_read.get(), bytes, sizeof(bytes));
    check(count > 0 && QByteArray(bytes, count) == "LISTENING 43210\nREADY\n",
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

void guest_link(QCoreApplication& app) {
    rwin::host::GuestLink link;
    check(link.listen(0, QHostAddress::LocalHost) && link.port() != 0,
          "OS allocates the actual listening port");
    QTcpSocket guest;
    guest.connectToHost(QHostAddress::LocalHost, link.port());
    check(guest.waitForConnected(1000), "guest can dial loopback");
    int connected = 0;
    int listed = 0;
    int disconnected = 0;
    int frames = 0;
    QObject::connect(&link, &rwin::host::GuestLink::connected, &app, [&] { ++connected; });
    QObject::connect(&link, &rwin::host::GuestLink::disconnected, &app, [&] { ++disconnected; });
    QObject::connect(&link, &rwin::host::GuestLink::frame_arrived, &app,
                     [&](quint64, const QImage& pixels) {
                         ++frames;
                         check(!pixels.isNull() && pixels.size() == QSize(1, 1),
                               "only a valid frame is emitted");
                     });
    QObject::connect(&link, &rwin::host::GuestLink::windows_listed, &app,
                     [&](const QList<rwin::host::GuestWindowInfo>& windows, bool, QSize) {
                         ++listed;
                         check(windows.size() == 1,
                               "only the valid minimum-sized window list is emitted");
                     });
    const auto send = [&](rwin::MessageWriter message) {
        const auto bytes = message.take();
        check(guest.write(reinterpret_cast<const char*>(bytes.data()),
                          static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()),
              "write guest message");
        guest.flush();
    };
    rwin::MessageWriter hello(rwin::MessageId::GuestHello);
    rwin::WireHello{}.write(hello);
    send(std::move(hello));
    check(wait_for([&] { return connected == 1; }), "handshake completes");
    rwin::MessageWriter malformed(rwin::MessageId::WindowList);
    malformed.put_u8(1);
    rwin::GuestDesktop{1920, 1080, true}.write(malformed);
    malformed.put_u32(0xffffffff);
    send(std::move(malformed));
    rwin::MessageWriter valid(rwin::MessageId::WindowList);
    valid.put_u8(1);
    rwin::GuestDesktop{1920, 1080, true}.write(valid);
    valid.put_u32(1);
    valid.put_u64(1);
    valid.put_u64(0);
    for (int i = 0; i != 8; ++i)
        valid.put_u32(0);
    valid.put_string("");
    valid.put_string("");
    valid.put_u8(0);
    valid.put_u32(0);
    valid.put_u8(255);
    for (int i = 0; i != 3; ++i)
        valid.put_u32(0);
    send(std::move(valid));
    check(wait_for([&] { return listed == 1; }),
          "malicious count is rejected without a huge allocation");
    rwin::MessageWriter huge_frame(rwin::MessageId::Frame);
    huge_frame.put_u64(1);
    huge_frame.put_u32(0x80000000);
    huge_frame.put_u32(2);
    send(std::move(huge_frame));
    rwin::MessageWriter good_frame(rwin::MessageId::Frame);
    good_frame.put_u64(1);
    good_frame.put_u32(1);
    good_frame.put_u32(1);
    good_frame.put_u32(0xffffffff);
    send(std::move(good_frame));
    check(wait_for([&] { return frames == 1; }),
          "overflowing frame dimensions do not create false readiness");
    guest.disconnectFromHost();
    check(wait_for([&] { return disconnected == 1; }), "guest disconnect is detected");
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    parent_control(app);
    guest_link(app);
    return failures == 0 ? 0 : 1;
}
