#pragma once

#include "host/chrome/resize_border.hpp"
#include "host/chrome/win32_style.hpp"
#include "host/cursor/worn_cursor.hpp"
#include "host/input/held_keys.hpp"
#include "host/input/key_translator.hpp"
#include "host/link/guest_window_info.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/picture_layout.hpp"

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QWidget>

class QPainter;

#include <cstddef>
#include <optional>

namespace rwin::host {

struct CursorLook;
class GpuPicture;
class GuestLink;
class PixelCursor;
class PixelMapping;
struct SharedFrame;

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
// widget - unless it is one the user types into and moves, a dialog
// (owned_window_stands_alone), which is a toplevel of its own attached to
// its owner's. Everything else is a toplevel the compositor places.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    // ``owner`` is the mirror of the window that owns this one, or null
    // when it has none or its owner is not mirrored.
    // ``title_tag`` is appended to the title, as what names this window to
    // the compositor (PlacementSync::tag_for); it is not drawn.
    // ``pixel_cursor`` puts the guest's own cursor pictures on it.
    MirrorWindow(GuestLink& link, const GuestWindowInfo& window, MirrorWindow* owner,
                 QString title_tag, PixelCursor& pixel_cursor);

    [[nodiscard]] std::uint64_t handle() const noexcept { return handle_; }

    // How many native surfaces this mirror has had: a new one is a window
    // the compositor has placed afresh.
    [[nodiscard]] std::uint32_t surface_generation() const noexcept { return surfaces_; }

    // What this mirror decided, for a harness to compare with the window.
    [[nodiscard]] MirrorState state() const;

    void present(const QImage& pixels);
    // Frames that stay on the GPU are drawn here from now on, as well as
    // frames of pixels. Best before the mirror is first shown: it makes the
    // mirror's window an OpenGL one (GpuPicture), and a window made one
    // after it is shown is made again.
    void take_gpu_frames();
    void present_shared(const SharedFrame& frame);
    void retitle(const QString& title);
    void note_guest_size(std::uint32_t width, std::uint32_t height);

    // The most the guest has room for: the whole of its screen less what
    // is reserved on it. A drag is held at this, so the user is stopped
    // by the guest's own limit rather than by the window springing back
    // from it — and a mirror maximised past it is filled by fitting the
    // guest's screen into it rather than by a window that cannot be that
    // large.
    void limit_to(QSize guest_room);

    // The guest's cursor, at one of its steps; null is no cursor at all. A
    // cursor Windows ships is worn as this desktop's own of the same
    // meaning, any other as the guest drew it.
    void wear_guest_cursor(const CursorLook* look, std::size_t step);

    // The guest window's picture size, in its pixels, and where a point of
    // it is in this mirror, in logical units.
    [[nodiscard]] QSize guest_picture_size() const { return picture_size(); }
    [[nodiscard]] QPointF logical_point_of(QPoint guest) const;
    // The whole logical units of this mirror wholly over a rectangle of
    // its guest picture.
    [[nodiscard]] QRect logical_rect_inside(QRect guest) const;

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

    // A toplevel of its own, attached to ``owner``'s mirror - kept above
    // it and grouped with it by the compositor - or, with none, standing
    // alone. A popup becomes a toplevel here: its owner is gone from this
    // side, or it is a window that stands alone.
    void attach_to(MirrorWindow* owner);

    // The user pressed on this window while the guest has it disabled -
    // behind its modal dialog. Windows activates the dialog and flashes
    // it; the press reaches nothing.
    //
    // This is that dialog: raise it, ask for it to be the active window,
    // and ask the desktop to draw the user's attention to it.
    void come_forward();

    // The compositor made another of this application's mirrors active on
    // its own - as it mapped it - while this is the guest's foreground:
    // ask for the activation back, since the guest's is the one followed.
    void reclaim_activation();

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
    // A press on this window while the guest has it disabled, which is
    // not passed on: who is to come forward instead is the session's to
    // know (MirrorSession::bring_forward_for).
    void pressed_while_disabled(std::uint64_t window);
    // Made active by the compositor as it mapped it, not by the user and
    // not because the guest made it its foreground.
    void focused_unasked(std::uint64_t window);
    // A frame that stayed on the GPU could not be drawn here.
    void gpu_failed(const QString& why);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    [[nodiscard]] Qt::WindowFlags flags_for(const Win32Style& style) const noexcept;
    [[nodiscard]] bool is_popup() const noexcept { return parentWidget() != nullptr; }
    // Shown again with the flags and transient parent it now has: a
    // surface keeps the role and parent it was made with.
    void remake_surface();
    void wear(const Win32Style& style);
    void take_shape(QList<QRect> region);
    void fill_bars(QPainter& painter, const QRectF& picture) const;
    void apply_shape();
    [[nodiscard]] Qt::Edges edges_at(QPointF position) const noexcept;
    [[nodiscard]] bool start_edge_resize(QMouseEvent* event);
    // How this mirror's guest pixels land in it: at its own screen's ratio
    // and its toplevel's fit, the picture centred.
    [[nodiscard]] PixelMapping mapping() const;
    [[nodiscard]] QSize picture_size() const;
    // The logical units the picture covers; the rest is letterbox bars.
    [[nodiscard]] QRect picture_rect() const;
    // What paintEvent would tell QPainter, for the picture drawn on the GPU.
    [[nodiscard]] PictureLayout gpu_layout() const;
    [[nodiscard]] bool shows_gpu_frame() const;
    [[nodiscard]] QSize logical_size() const;
    void refit();
    void follow_root_scale();
    void follow_device_pixel_ratio();
    // The guest's cursor on this window, unless the pointer is in the
    // resize band, where the band's is.
    void show_guest_cursor();
    void describe_cursor(MirrorState& state) const;
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
    QString title_tag_;
    Win32Style style_;
    KeyTranslator keys_;
    HeldKeys held_keys_;
    QImage frame_;
    // A colour-keyed window's frame after keying; reused frame to frame.
    QImage keyed_;
    // The picture when the guest's frames stay on the GPU; null otherwise.
    GpuPicture* gpu_ = nullptr;
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
    std::uint64_t frames_presented_ = 0;
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
    // Where this popup asked to be, in the units pos() is in. The
    // compositor answers with a place of its own, on its own grid.
    QPoint placed_;
    bool maximized_ = false;
    // The mirror this toplevel is attached to, its owner's; null for one
    // standing alone, and for a popup, whose owner is its parent widget.
    QPointer<MirrorWindow> attached_to_;
    // The guest's foreground, as last reported: a request to be the active
    // window is made when it becomes this window, not every time it is
    // said again.
    bool guest_foreground_ = false;
    std::uint32_t surfaces_ = 0;
    std::uint32_t activation_requests_ = 0;
    std::uint32_t alerts_ = 0;
    // Since it was last shown or minimised for the guest, and whether the
    // user has pressed on it or typed into it since: focus that arrives
    // soon after either, with neither, is the compositor's - given as it
    // mapped the window, or handed back to it as its dialogs closed.
    QElapsedTimer changed_for_guest_;
    bool touched_ = false;
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
    // The guest window's own state, as last reported.
    bool guest_maximized_ = false;
    ResizeBorder border_;
    WornCursor worn_;
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
