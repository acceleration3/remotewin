#pragma once

#include "host/cursor/cursor_player.hpp"
#include "host/cursor/pixel_cursor.hpp"
#include "host/mirror/monitor_window.hpp"
#include "host/wayland/pointer_globals.hpp"
#include "proto/window_id.hpp"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QSize>
#include <QString>

namespace rwin::host {

class GuestLink;
class SharedFrameFeed;
struct SharedFrame;

// The guest's whole primary monitor, mirrored as one window for as long as
// that window is open (WatchMonitor): its pixels, its cursor, and the
// pointer and keys put back into it.
//
// The pointer is not held, locked or confined here whatever the guest's
// applications ask: the window is the user's way into the whole guest, and
// it lets go of the pointer as any window does.
class MonitorSession : public QObject {
    Q_OBJECT
public:
    // ``frames`` hands on the frames that stay on the GPU, when it was
    // opened: the window draws those as well as frames of pixels.
    MonitorSession(GuestLink& link, SharedFrameFeed& frames, const QString& title,
                   QObject* parent = nullptr, QWidget* view_parent = nullptr);
    // A child view for an embedded preview; otherwise an ordinary desktop window.
    [[nodiscard]] MonitorWindow& view() noexcept { return window_; }
    void raise();

Q_SIGNALS:
    // The window was closed, or the guest can no longer capture its monitor.
    void finished();
    void ready();

private:
    void show_frame(WindowId window, const QImage& pixels);
    void show_patch(WindowId window, QSize picture, QPoint at, const QImage& pixels);
    void show_shared_frame(WindowId window, const SharedFrame& frame);
    void surface_lost(WindowId window);
    void note_ready();

    GuestLink& link_;
    // Bound before anything that sizes the cursor.
    PointerGlobals pointer_globals_;
    PixelCursor pixel_cursor_{pointer_globals_};
    CursorPlayer cursor_;
    MonitorWindow window_;
    bool ready_ = false;
};

}  // namespace rwin::host
