#include "host/cursor/pixel_cursor.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/mirror_window.hpp"
#include "host/wayland/pointer_globals.hpp"

#include <QApplication>
#include <QEvent>
#include <QMouseEvent>
#include <QPointer>
#include <cstdio>

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
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    rwin::host::GuestLink link;
    rwin::host::PointerGlobals globals;
    rwin::host::PixelCursor cursor(globals);
    rwin::host::GuestWindowInfo window;
    window.handle = 1;
    window.width = 200;
    window.height = 120;
    window.style = rwin::host::Win32Style::kWsCaption;
    window.title = QStringLiteral("lifecycle regression");
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
    window.style |= rwin::host::Win32Style::kWsDisabled;
    mirror->follow_guest_style(window);
    mouse(*mirror, QEvent::MouseButtonRelease);
    check(!mirror->holds_press() && released == 1,
          "disabling a pressed window does not lose its release");
    mirror->guest_closed();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    check(mirror.isNull(), "guest closure destroys the local mirror");

    window.style &= ~rwin::host::Win32Style::kWsDisabled;
    mirror = new rwin::host::MirrorWindow(link, window, nullptr, {}, cursor);
    mirror->present(frame);
    mirror->show();
    QApplication::processEvents();
    mouse(*mirror, QEvent::MouseButtonPress);
    mirror->take_over_drag();
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
    return failures == 0 ? 0 : 1;
}
