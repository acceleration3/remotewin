#include "host/cursor/pixel_cursor.hpp"
#include "host/link/guest_link.hpp"
#include "host/link/guest_window.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "proto/frame_decoder.hpp"
#include "proto/hello.hpp"
#include "proto/message_reader.hpp"
#include "proto/message_writer.hpp"
#include "proto/move_size_kind.hpp"
#include "proto/window_fields.hpp"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QTcpSocket>
#include <QWindow>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* label) {
    if (!ok) {
        std::printf("FAIL: %s\n", label);
        ++failures;
    }
}

void mouse(QWidget& window, QEvent::Type type) {
    QMouseEvent event(type, QPointF(40, 40), QPointF(40, 40), Qt::LeftButton,
                      type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&window, &event);
}

// The guest's end of the link: what the mirrors send it, by message.
class Listener {
public:
    explicit Listener(rwin::host::GuestLink& link) {
        check(link.listen(0, QHostAddress::LocalHost), "the link listens");
        socket_.connectToHost(QHostAddress::LocalHost, link.port());
        check(socket_.waitForConnected(1000), "the guest dials in");
        rwin::MessageWriter hello{rwin::MessageId::GuestHello};
        rwin::Hello::ours().write(hello);
        const std::vector<std::uint8_t> bytes = hello.take();
        socket_.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
        socket_.flush();
        QObject::connect(&link, &rwin::host::GuestLink::connected, &socket_, [this] { connected_ = true; });
        wait([this] { return connected_; });
    }

    // Whether a message of ``id`` arrives, and with it every one before.
    bool heard(rwin::MessageId id) {
        return wait([&] {
            const QByteArray chunk = socket_.readAll();
            decoder_.feed(reinterpret_cast<const std::uint8_t*>(chunk.constData()),
                          static_cast<std::size_t>(chunk.size()));
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            bool found = false;
            while (decoder_.next(&payload, &size)) {
                found = found || rwin::MessageReader{payload, size}.id() == id;
            }
            return found;
        });
    }

private:
    template <typename Done>
    static bool wait(Done done) {
        for (int round = 0; round < 200; ++round) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            if (done()) return true;
        }
        return false;
    }

    QTcpSocket socket_;
    rwin::FrameDecoder decoder_;
    bool connected_ = false;
};

// A dialog restyled - pinned on top, say - is made a new surface, which is
// still attached to its owner's.
void a_restyled_dialog_keeps_its_parent(rwin::host::GuestLink& link, rwin::host::PixelCursor& cursor,
                                        const rwin::host::GuestWindow& owner_window) {
    rwin::WindowFields fields = owner_window.fields();
    fields.owner = owner_window.id();
    rwin::host::GuestWindow dialog_window{rwin::WindowId{2, 1}, fields};
    QPointer<rwin::host::MirrorWindow> owner = new rwin::host::MirrorWindow(link, owner_window, nullptr, {}, cursor);
    QPointer<rwin::host::MirrorWindow> dialog = new rwin::host::MirrorWindow(link, dialog_window, nullptr, {}, cursor);
    owner->show();
    dialog->attach_to(owner);
    dialog->show();
    QApplication::processEvents();
    fields.style.extended |= rwin::host::Win32Style::kWsExTopMost;
    dialog_window.take(fields);
    dialog->follow_guest_style(dialog_window);
    QApplication::processEvents();
    check(dialog->windowHandle() != nullptr && dialog->windowHandle()->transientParent() == owner->windowHandle(),
          "a restyled dialog is still attached to its owner");
    dialog->guest_closed();
    owner->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

// Close is the guest's to do (SC_CLOSE), and a pointer leaving is said.
void the_guest_is_told(rwin::host::PixelCursor& cursor, const rwin::host::GuestWindow& window) {
    rwin::host::GuestLink link;
    Listener guest{link};
    QPointer<rwin::host::MirrorWindow> mirror = new rwin::host::MirrorWindow(link, window, nullptr, {}, cursor);
    mirror->show();
    QApplication::processEvents();
    QEvent leave{QEvent::Leave};
    QApplication::sendEvent(mirror, &leave);
    check(guest.heard(rwin::MessageId::PointerLeave), "the pointer leaving a mirror is said");
    mirror->close();
    check(guest.heard(rwin::MessageId::SysCommand), "closing a mirror asks the guest to close its window");
    check(!mirror.isNull() && mirror->isVisible(), "and the mirror stays until the guest says the window is gone");
    mirror->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    rwin::host::GuestLink link;
    rwin::host::PointerGlobals globals;
    rwin::host::PixelCursor cursor(globals);
    rwin::WindowFields fields;
    fields.show = rwin::WindowFields::Show::Shown;
    fields.geometry.frame = {0, 0, 200, 120};
    fields.geometry.picture = {0, 0, 200, 120};
    fields.style.style = rwin::host::Win32Style::kWsCaption;
    fields.title = "lifecycle regression";
    rwin::host::GuestWindow window{rwin::WindowId{1, 1}, fields};
    QImage frame(200, 120, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::white);

    QPointer<rwin::host::MirrorWindow> mirror =
        new rwin::host::MirrorWindow(link, window, nullptr, {}, cursor);
    mirror->present(frame);
    mirror->show();
    QApplication::processEvents();
    mouse(*mirror, QEvent::MouseButtonPress);
    check(mirror->holds_press(), "forwarded press is tracked");
    int released = 0;
    QObject::connect(mirror, &rwin::host::MirrorWindow::let_go, &app, [&] { ++released; });
    fields.style.style |= rwin::host::Win32Style::kWsDisabled;
    window.take(fields);
    mirror->follow_guest_style(window);
    mouse(*mirror, QEvent::MouseButtonRelease);
    check(!mirror->holds_press() && released == 1,
          "disabling a pressed window does not lose its release");
    mirror->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(mirror.isNull(), "guest closure destroys the local mirror");

    fields.style.style &= ~rwin::host::Win32Style::kWsDisabled;
    window.take(fields);
    mirror = new rwin::host::MirrorWindow(link, window, nullptr, {}, cursor);
    mirror->present(frame);
    mirror->show();
    QApplication::processEvents();
    mouse(*mirror, QEvent::MouseButtonPress);
    mirror->take_over_move_size(rwin::MoveSizeKind::Move);
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        check(!mirror->holds_press(), "compositor drag no longer waits for a swallowed release");
    }
    mirror->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(mirror.isNull(), "dragged guest window leaves no stale mirror");

    // Windows says a minimised window is not maximised, and restores it
    // maximised.
    mirror = new rwin::host::MirrorWindow(link, window, nullptr, {}, cursor);
    mirror->present(frame);
    mirror->follow_guest_state(false, true);
    mirror->show();
    QApplication::processEvents();
    mirror->follow_guest_state(true, false);
    QApplication::processEvents();
    check(mirror->isMinimized(), "a guest window minimised is minimised here");
    check(mirror->isMaximized(), "minimised, a maximised window is still maximised");
    mirror->follow_guest_state(false, true);
    QApplication::processEvents();
    check(!mirror->isMinimized() && mirror->isMaximized(),
          "a maximised window restored from minimised comes back maximised");
    mirror->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    a_restyled_dialog_keeps_its_parent(link, cursor, window);
    the_guest_is_told(cursor, window);
    return failures == 0 ? 0 : 1;
}
