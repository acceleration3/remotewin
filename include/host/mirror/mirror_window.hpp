#pragma once

#include "host/chrome/resize_border.hpp"
#include "host/chrome/win32_style.hpp"
#include "host/cursor/worn_cursor.hpp"
#include "host/input/held_keys.hpp"
#include "host/input/key_translator.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/mirror/mirror_state.hpp"
#include "host/mirror/picture_layout.hpp"
#include "proto/min_max.hpp"
#include "proto/move_size_kind.hpp"
#include "proto/window_id.hpp"

#include <QImage>
#include <QList>
#include <QMargins>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QWidget>

class QPainter;
class QTimer;

#include <cstddef>
#include <optional>

namespace rwin::host {

struct CursorLook;
class GpuPicture;
class GuestLink;
class GuestWindow;
class PixelCursor;
class PixelMapping;
struct SharedFrame;

// One guest window, shown as a window of this desktop that behaves the
// way its Win32 style words say a window behaves.
//
// Frameless: what looks like a title bar is the guest's own, arriving in
// the pixels. Nothing about that chrome is interpreted here — a press on
// it is forwarded, and if Windows begins moving or sizing the window the
// guest says so, and the compositor is handed the gesture. Everything
// else is read off the style words: whether the edges resize, whether it
// takes focus, whether it takes input at all.
//
// A window placed against another - owned by it, or a menu dropped from
// it - is placed where the guest put it, relative to that window. Wayland
// has no global coordinates, and a popup positioned by an offset from its
// parent surface is the one surface a client places; so such a window is
// that, a child of the other's mirror - unless it is one the user types
// into and moves, a dialog (Win32Style::stands_alone), which is a toplevel
// of its own attached to its owner's. Everything else is a toplevel the
// compositor places.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    // ``popup_of`` is the mirror this one is made a popup of, or null for a
    // toplevel. ``title_tag`` is appended to the title, as what names this
    // window to the compositor (PlacementSync::tag_for); it is not drawn.
    // ``pixel_cursor`` puts the guest's own cursor pictures on it.
    MirrorWindow(GuestLink& link, const GuestWindow& window, MirrorWindow* popup_of, QString title_tag,
                 PixelCursor& pixel_cursor);

    [[nodiscard]] WindowId window() const noexcept { return window_; }

    // How many native surfaces this mirror has had: a new one is a window
    // the compositor has placed afresh.
    [[nodiscard]] std::uint32_t surface_generation() const noexcept { return surfaces_; }

    // What this mirror decided, for a harness to compare with the window.
    [[nodiscard]] MirrorState state() const;
    // What this mirror shows, in its device pixels: its own pixels, and the
    // picture drawn from the GPU over them, as the compositor puts the one
    // on the other. For a harness.
    [[nodiscard]] QImage grab_shown();

    void present(const QImage& pixels);
    // Frames that stay on the GPU are drawn here from now on, as well as
    // frames of pixels: in a window of their own over this one's
    // (GpuPicture), which the compositor holds as a subsurface of it.
    void take_gpu_frames();
    void present_shared(const SharedFrame& frame);
    void retitle(const QString& title);

    // Where the guest window is, how large, its shape and its borders, as
    // the guest last said them.
    void follow_guest_geometry(const GuestWindow& window);
    // What Windows last said the window's size may be (MinMax).
    void follow_guest_limits(const std::optional<MinMax>& limits);

    // The guest's work area: the whole of its screen less what is reserved
    // on it. A drag is held at this, so the user is stopped by the guest's
    // own limit rather than by the window springing back from it.
    void limit_to(QSize work_area);

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
    void follow_guest_style(const GuestWindow& window);

    // Windows began moving or sizing this window (MoveSizeStart). When a
    // press here began it, the compositor is asked to run that gesture
    // instead, and if it does, the guest is given the press's release at
    // once: Windows' own loop ends where it began, having moved nothing.
    // A loop from the keyboard, or one the compositor will not take, is
    // left to Windows.
    void take_over_move_size(MoveSizeKind kind);

    // Follow the state the guest window ended up in. That is how a press
    // on chrome this side does not interpret still reaches this window.
    void follow_guest_state(bool minimized, bool maximized);
    // Out of minimised, back into the state it was minimised from:
    // showNormal would take a maximised mirror out of maximised as well.
    void unminimize();
    // Whether the pointer's own motion reaches held_motion: then a button
    // held in this mirror moves the guest's pointer by that motion, from
    // where the press was, as PointerAt.
    void follow_relative_motion(bool follows) noexcept { relative_motion_ = follows; }
    // A button pressed here is held, and the pointer goes by its motion.
    [[nodiscard]] bool holds() const noexcept { return held_at_.has_value(); }
    // The pointer moved by ``logical`` units of this surface while held.
    void held_motion(QPointF logical);

    // Whether the guest has this as its active window: then showing it
    // here may activate it too.
    void note_guest_active(bool active);
    // The guest made this its active window while the user is in this
    // application: be the active one here.
    void activate_for_guest();

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
    void pressed_while_disabled(rwin::WindowId window);
    // This desktop gave the mirror the keyboard focus; ``by_press`` when a
    // button was down as it did, which is a press in it activating it -
    // the guest is given the press, and activates the window by itself.
    void focused(rwin::WindowId window, bool by_press);
    // A frame that stayed on the GPU could not be drawn here.
    void gpu_failed(const QString& why);
    // A frame drawn from the GPU reached the screen (GpuPicture::frame_timed).
    void frame_timed(const rwin::host::FrameStamps& stamps);
    // A new native surface was made for this mirror - a restyle remakes
    // it - which the compositor places afresh, wherever it likes.
    void surface_made(rwin::WindowId window);

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
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    [[nodiscard]] Qt::WindowFlags flags_for(const Win32Style& style) const noexcept;
    [[nodiscard]] bool is_popup() const noexcept { return parentWidget() != nullptr; }
    // Shown again with the flags and transient parent it now has: a
    // surface keeps the role and parent it was made with.
    void remake_surface();
    // The native window, made now, with the transient parent it is to
    // have: a toplevel attached to its owner is made with that parent.
    void make_surface();
    void wear(const Win32Style& style);
    void take_shape(QList<QRect> region);
    void fill_bars(QPainter& painter, const QRectF& picture) const;
    void apply_shape();
    void fit_border();
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
    void note_guest_size(QSize reported);
    void settle_request(QSize reported);
    void apply_size_limits();
    // The picture sizes Windows' size limits allow, at most the work area.
    [[nodiscard]] QSize smallest_picture() const;
    [[nodiscard]] QSize largest_picture() const;
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
    // The same, outside the picture too: for a button the guest holds.
    [[nodiscard]] QPoint guest_point_of(QMouseEvent* event) const;
    void send_key(QKeyEvent* event, bool pressed);
    // The window's menu, for a right press on the bars around its picture.
    void open_window_menu(QMouseEvent* event);
    void ask_guest_for(QSize size);

    GuestLink& link_;
    WindowId window_;
    QString title_tag_;
    Win32Style style_;
    KeyTranslator keys_;
    HeldKeys held_keys_;
    QImage frame_;
    // A colour-keyed window's frame after keying; reused frame to frame.
    QImage keyed_;
    // The picture when the guest's frames stay on the GPU; null otherwise.
    GpuPicture* gpu_ = nullptr;
    // What shows gpu_ inside this widget; it owns gpu_.
    QWidget* gpu_view_ = nullptr;
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
    // A size the guest cannot take leaves its window as it was, which it
    // has nothing to say about: the wait for an answer ends with this.
    QTimer* answer_due_;
    QSize work_area_;
    // Where the picture sits in the window's frame, and how much larger
    // than the picture the frame is: what a picture size asked for is told
    // to the guest as (WindowMove).
    QPoint picture_offset_;
    QSize picture_padding_;
    // The window's invisible resize borders, in its pixels.
    QMargins guest_margins_;
    std::optional<MinMax> limits_;
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
    bool guest_active_ = false;
    std::uint32_t surfaces_ = 0;
    std::uint32_t activation_requests_ = 0;
    std::uint32_t alerts_ = 0;
    // Whether this mirror was minimised for the guest. Kept, because
    // Wayland tells a client nothing about being minimised: Qt's own flag
    // stays false through it, so a guest window coming back out of
    // minimised was compared against a mirror that claimed never to have
    // gone in, and the mirror stayed away.
    bool minimized_ = false;
    // The guest window's own state, as last reported. A mirror maximised
    // or restored here that already agrees with it was the guest's doing
    // followed, and the guest is told nothing.
    bool guest_maximized_ = false;
    ResizeBorder border_;
    WornCursor worn_;
    Qt::Edges hovered_edges_;
    Qt::MouseButtons held_;
    // Where the guest window's picture is on the guest's screen.
    QPoint guest_position_;
    // The last press forwarded, in the toplevel's picture: where a gesture
    // the compositor takes over from Windows lets the button go.
    QPoint pressed_at_;
    std::uint8_t pressed_button_ = 0;
    // While a button pressed here is held, where the guest's pointer is,
    // in its screen's pixels: the press, plus the pointer's motion since.
    std::optional<QPointF> held_at_;
    bool relative_motion_ = false;
    bool closing_when_released_ = false;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
    bool applying_guest_state_ = false;
    // The press that began a drag or a resize was consumed by it; its
    // release would otherwise reach the guest as a click nobody made.
    bool swallow_release_ = false;
};

}  // namespace rwin::host
