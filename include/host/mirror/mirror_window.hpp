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

#include <optional>

namespace rwin::host {

class GuestLink;
class PixelMapping;

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

    // The most the guest has room for: the whole of its screen less what
    // is reserved on it. A drag is held at this, so the user is stopped
    // by the guest's own limit rather than by the window springing back
    // from it — and a mirror maximised past it is filled by fitting the
    // guest's screen into it rather than by a window that cannot be that
    // large.
    void limit_to(QSize guest_room);

    // The cursor as the guest drew it, in its pixels.
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

    // A button pressed here and not yet released, on this window or on a
    // popup of it. The compositor holds the pointer to the surface it was
    // pressed on until the release, and delivers nothing to anyone if that
    // surface is gone — the release included.
    [[nodiscard]] bool holds_press() const noexcept;

    // The guest window is gone but a press is still held: stay, showing
    // nothing, until the release has been delivered, then close.
    void close_when_released();

Q_SIGNALS:
    // The last held button was released on a window waiting for it.
    void let_go();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;

private:
    [[nodiscard]] Qt::WindowFlags flags_for(const Win32Style& style) const noexcept;
    void wear(const Win32Style& style);
    void take_shape(QList<QRect> region);
    void apply_shape();
    [[nodiscard]] Qt::Edges edges_at(QPointF position) const noexcept;
    [[nodiscard]] bool start_edge_resize(QMouseEvent* event);
    // How this mirror's guest pixels land in it: at its own screen's ratio
    // and its toplevel's fit, the picture centred.
    [[nodiscard]] PixelMapping mapping() const;
    [[nodiscard]] QSize picture_size() const;
    // The logical units the picture covers; the rest is letterbox bars.
    [[nodiscard]] QRect picture_rect() const;
    [[nodiscard]] QSize logical_size() const;
    void refit();
    void follow_root_scale();
    void follow_device_pixel_ratio();
    [[nodiscard]] QCursor dressed(const QCursor& cursor) const;
    void settle_request(QSize reported);
    void apply_size_limits();
    // The toplevel this window descends from: itself, or the owner of the
    // owner of ... this popup.
    [[nodiscard]] const MirrorWindow& root() const;
    // Where this window is in the guest, relative to its toplevel.
    [[nodiscard]] QPoint guest_origin() const;
    // Which guest pixel the pointer is on, or nothing when it is on a
    // letterbox bar: that is this desktop's, not any part of the window,
    // and a press there put through would land on whatever the guest has
    // at a place the window does not cover.
    [[nodiscard]] std::optional<QPoint> guest_position_of(QMouseEvent* event) const;
    void send_key(QKeyEvent* event, bool pressed);
    void ask_guest_for(QSize size);

    GuestLink& link_;
    std::uint64_t handle_;
    Win32Style style_;
    QImage frame_;
    // A colour-keyed window's frame after keying; reused frame to frame.
    QImage keyed_;
    // The guest window's size, in its own pixels.
    //
    // One guest pixel is one device pixel here. A mirror's logical size is
    // the window's pixel size divided by the scale of the screen it is on,
    // and its picture is copied onto that screen's pixels, never resampled.
    // One pixel to one logical unit, as it once was, is a picture the
    // compositor stretches by the scale - 1.2 on a desktop at 120% - and a
    // stretch by anything not whole blurs or duplicates rows. That this is
    // also the right size on the glass is the guest's doing: its DPI is set
    // to this desktop's scale by whoever runs it, so it lays itself out as
    // large as a native application would. Positions, offsets and shapes
    // follow the same rule, through PixelMapping.
    QSize guest_size_;
    // A resize is asked for one at a time, in guest pixels. The compositor reports a new
    // size for every step of a drag, tens of them a second, and a guest
    // application laying itself out for each falls behind and answers
    // sizes the user has long since dragged past. So the newest size
    // waits for the last one to be answered, and the ones between are
    // never asked for at all.
    QSize awaiting_;
    QSize pending_;
    QSize guest_room_;
    // The guest window's own region, in the guest's pixels. Kept because
    // where it lands here moves with the scale.
    QList<QRect> guest_region_;
    // Device pixels per guest pixel, kept by the toplevel; a popup is drawn
    // and placed at its owner's, or it would not land where the guest put
    // it.
    double fit_ = 1.0;
    // Where the guest put this window relative to its owner, in guest
    // pixels. Input is measured from here rather than from where the popup
    // ended up: that is a whole number of logical units, rounded, and the
    // compositor may have slid it to keep it on screen. A pixel of the
    // popup is a pixel of the guest's window wherever it is shown.
    QPoint guest_offset_;
    bool maximized_ = false;
    // Whether this mirror was minimised for the guest. Kept, because
    // Wayland tells a client nothing about being minimised: Qt's own flag
    // stays false through it, so a guest window coming back out of
    // minimised was compared against a mirror that claimed never to have
    // gone in, and the mirror stayed away.
    bool minimized_ = false;
    // Whether it was the guest's own maximise that maximised this mirror.
    // Which side began it decides what the other is told: a mirror
    // maximised here asks the guest for its whole screen, and one
    // maximised because the guest maximised asks for nothing, the guest
    // being as large as it goes already.
    bool maximized_by_guest_ = false;
    ResizeBorder border_;
    QCursor guest_cursor_;
    Qt::Edges hovered_edges_;
    Qt::MouseButtons held_;
    bool closing_when_released_ = false;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
    bool applying_guest_state_ = false;
    // The press that began a drag or a resize was consumed by it; its
    // release would otherwise reach the guest as a click nobody made.
    bool swallow_release_ = false;
};

}  // namespace rwin::host
