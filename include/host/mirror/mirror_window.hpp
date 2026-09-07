#pragma once

#include "host/chrome/resize_border.hpp"
#include "host/chrome/win32_style.hpp"
#include "host/link/guest_window_info.hpp"

#include <QCursor>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QWidget>

namespace rwin::host {

class GuestLink;

// One guest window, shown as a window of this desktop that behaves the
// way its Win32 style words say a window behaves.
//
// Frameless: what looks like a title bar is the guest's own, arriving in
// the pixels. Nothing about that chrome is interpreted here — a press on
// it is forwarded, and if Windows decides it began a drag the guest says
// so. Everything else is read off the style words: whether the edges
// resize, whether it takes focus, whether it takes input at all.
//
// An owned window is placed where the guest put it, relative to its
// owner. Wayland has no global coordinates, and a popup positioned by an
// offset from its parent surface is the one surface a client places; so
// an owned window whose owner is mirrored is that, a child of the owner
// widget, and everything else is a toplevel the compositor places.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    // ``owner`` is the mirror of the window that owns this one, or null
    // when it has none or its owner is not mirrored.
    MirrorWindow(GuestLink& link, const GuestWindowInfo& window, MirrorWindow* owner);

    [[nodiscard]] std::uint64_t handle() const noexcept { return handle_; }

    void present(const QImage& pixels);
    void retitle(const QString& title);
    void note_guest_size(std::uint32_t width, std::uint32_t height);
    void wear_guest_cursor(const QCursor& cursor);

    // The style words as the guest last reported them. A window changes
    // them while it lives — disabled behind its modal dialog, pinned on
    // top — and the mirror changes with it.
    void follow_guest_style(const GuestWindowInfo& window);

    // Windows started moving this window and the guest stopped it. The
    // drag belongs to the compositor that actually has the window.
    void take_over_drag();

    // Follow the state the guest window ended up in. That is how a press
    // on chrome this side does not interpret still reaches this window.
    void follow_guest_state(bool minimized, bool maximized);

    // The guest made this its foreground window; be the active one here.
    void follow_guest_foreground(bool foreground);

    // Put this window where the guest put it, relative to its owner.
    // ``guest_offset`` is in guest pixels. A mirror that was a toplevel
    // because its owner had not been seen becomes the owner's popup here.
    void place_under(MirrorWindow& owner, QPoint guest_offset);

    // The guest window is verifiably gone: close without asking the guest
    // to close a window that no longer exists.
    void guest_closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    [[nodiscard]] Qt::WindowFlags flags_for(const Win32Style& style) const noexcept;
    void wear(const Win32Style& style);
    void take_shape(const QList<QRect>& region);
    [[nodiscard]] Qt::Edges edges_at(QPointF position) const noexcept;
    [[nodiscard]] bool start_edge_resize(QMouseEvent* event);
    [[nodiscard]] QPoint frame_origin() const;
    [[nodiscard]] QPoint guest_position_of(QMouseEvent* event) const;
    void send_key(QKeyEvent* event, bool pressed);

    GuestLink& link_;
    std::uint64_t handle_;
    Win32Style style_;
    QImage frame_;
    // A colour-keyed window's frame after keying; reused frame to frame.
    QImage keyed_;
    // One guest pixel is one logical unit here, deliberately.
    //
    // A scaled desktop makes those different sizes on the glass, and that
    // is the point: a native window of N units and a guest window of N
    // pixels take up the same share of their own screen, so the mirror
    // keeps the proportions the application was laid out for. Dividing by
    // the scale would shrink every mirror below what it looks like in the
    // guest. Positions and offsets follow the same rule, which is what
    // keeps a popup where the guest put it.
    QSize guest_size_;
    ResizeBorder border_;
    QCursor guest_cursor_;
    Qt::Edges hovered_edges_;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
    bool applying_guest_state_ = false;
    // The press that began a drag or a resize was consumed by it; its
    // release would otherwise reach the guest as a click nobody made.
    bool swallow_release_ = false;
};

}  // namespace rwin::host
