#include "host/mirror/monitor_session.hpp"

#include "host/export/shared_frame.hpp"
#include "host/export/shared_frame_feed.hpp"
#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/cursor_picture.hpp"
#include "proto/guest_cursor.hpp"
#include "proto/message_id.hpp"

namespace rwin::host {

MonitorSession::MonitorSession(GuestLink& link, SharedFrameFeed& frames, const QString& title,
                               QObject* parent, QWidget* view_parent)
    : QObject(parent), link_(link), window_(link, title, pixel_cursor_, view_parent) {
    if (frames.offered()) {
        window_.take_gpu_frames();
        connect(&frames, &SharedFrameFeed::frame_ready, this, &MonitorSession::show_shared_frame);
        connect(&window_, &MonitorWindow::gpu_failed, this,
                [&frames](const QString& why) { frames.give_up(why.toStdString()); });
    }
    // Asked once the guest has said it speaks this wire: asked before, the
    // request goes to nobody, and asked again, a capture is made again.
    connect(&link_, &GuestLink::connected, this, [this] {
        link_.watch_monitor();
        window_.note_guest_connected();
    });
    connect(&link_, &GuestLink::frame_arrived, this, &MonitorSession::show_frame);
    connect(&link_, &GuestLink::frame_patched, this, &MonitorSession::show_patch);
    connect(&link_, &GuestLink::window_gone, this, &MonitorSession::forget);
    connect(&link_, &GuestLink::cursor_changed, &cursor_, &CursorPlayer::show);
    connect(&link_, &GuestLink::cursor_pictured, &cursor_, &CursorPlayer::remember);
    connect(&cursor_, &CursorPlayer::worn, &window_, &MonitorWindow::wear_guest_cursor);
    connect(&window_, &MonitorWindow::closed, this, &MonitorSession::finished);
    window_.show();
}

void MonitorSession::show_frame(quint64 window, const QImage& pixels) {
    if (window != kMonitorWindow)
        return;
    window_.present(pixels);
    if (!ready_) {
        ready_ = true;
        Q_EMIT ready();
    }
}

void MonitorSession::raise() {
    if (window_.isMinimized())
        window_.showNormal();
    window_.raise();
    window_.activateWindow();
}

void MonitorSession::show_patch(quint64 window, QSize picture, QPoint at, const QImage& pixels) {
    if (window == kMonitorWindow)
        window_.patch(picture, at, pixels);
}

void MonitorSession::show_shared_frame(quint64 window, const SharedFrame& frame) {
    if (window != kMonitorWindow)
        return;
    window_.present_shared(frame);
    if (!ready_) {
        ready_ = true;
        Q_EMIT ready();
    }
}

void MonitorSession::forget(quint64 window) {
    if (window != kMonitorWindow)
        return;
    RWIN_ERROR("monitor", "the guest can no longer capture its monitor");
    Q_EMIT finished();
}

}  // namespace rwin::host
