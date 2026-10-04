#include "host/mirror/mirror_window.hpp"

#include "host/link/guest_link.hpp"
#include "host/mirror/draw_picture.hpp"
#include "host/mirror/pixel_mapping.hpp"
#include "logging/logger.hpp"

#include <QCloseEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QResizeEvent>
#include <QWindow>

#include <cmath>
#include <utility>

namespace rwin::host {

namespace {

// Only the keys a character cannot express; everything printable arrives
// as text and is typed as a character on the other side.
std::uint32_t virtual_key_for(int key) {
    switch (key) {
        case Qt::Key_Backspace: return 0x08;
        case Qt::Key_Tab:       return 0x09;
        case Qt::Key_Return:
        case Qt::Key_Enter:     return 0x0D;
        case Qt::Key_Escape:    return 0x1B;
        case Qt::Key_Delete:    return 0x2E;
        case Qt::Key_Home:      return 0x24;
        case Qt::Key_End:       return 0x23;
        case Qt::Key_Left:      return 0x25;
        case Qt::Key_Up:        return 0x26;
        case Qt::Key_Right:     return 0x27;
        case Qt::Key_Down:      return 0x28;
        case Qt::Key_PageUp:    return 0x21;
        case Qt::Key_PageDown:  return 0x22;
        default:                return 0;
    }
}

std::uint8_t button_index(Qt::MouseButton button) {
    if (button == Qt::MiddleButton) return 1;
    if (button == Qt::RightButton) return 2;
    return 0;
}

}  // namespace

MirrorWindow::MirrorWindow(GuestLink& link, const GuestWindowInfo& window,
                           MirrorWindow* owner)
    : QWidget(owner),
      link_(link),
      handle_(window.handle),
      style_(window),
      guest_size_(static_cast<int>(window.width), static_cast<int>(window.height)) {
    setWindowFlags(flags_for(style_));
    setWindowTitle(window.title.isEmpty() ? QStringLiteral("Guest window")
                                          : window.title);
    setAttribute(Qt::WA_DeleteOnClose);
    // The guest's pixels carry their own alpha. Without this the widget
    // paints an opaque background first and every transparent part of the
    // window — a rounded corner, a shadow, a see-through panel — lands on
    // top of it.
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    // At the ratio of the screen it will open on, which on a fractionally
    // scaled desktop is the scale rounded up to a whole one: the surface
    // learns its own only once mapped, and is resized then, before its
    // first frame is drawn (follow_device_pixel_ratio).
    resize(logical_size());
    wear(style_);
    // Made as a popup of a mirror that is already fitted: it is drawn at
    // that mirror's scale from its first frame, not from its first
    // change of size.
    if (owner != nullptr) follow_root_scale();
    take_shape(window.region);
    RWIN_DEBUG("win32",
               "{:#x} {}x{} owned={} sizable={} activates={} taskbar={} topmost={} "
               "disabled={} shadow={} opacity={} keyed={} shaped={}",
               handle_, window.width, window.height, style_.owned(), style_.sizable(),
               style_.activates(), style_.in_taskbar(), style_.topmost(),
               style_.disabled(), style_.casts_shadow(), style_.opacity(),
               style_.color_keyed(), !window.region.isEmpty());
}

// A window region shapes both what is seen and what is hit: a point
// outside it belongs to whatever is behind. Qt's mask is the same two
// things — the painted area and the input region of the surface.
void MirrorWindow::take_shape(QList<QRect> region) {
    if (region == guest_region_) return;
    guest_region_ = std::move(region);
    apply_shape();
}

// The region is where the picture is, so it is placed and scaled with the
// picture. Left in the guest's own pixels it clipped a fitted mirror to
// the top corner of itself: the rest was painted as nothing and, being
// outside the input region a mask also sets, took no clicks either.
//
// A mask is in whole logical units, which a guest pixel edge falls between
// on a fractionally scaled screen; each rectangle is rounded outward, so a
// shape is at most a device pixel generous and never cuts into the window.
void MirrorWindow::apply_shape() {
    if (guest_region_.isEmpty()) {
        if (!mask().isEmpty()) clearMask();
        return;
    }
    const PixelMapping where = mapping();
    // The bars are the mirror's, not the window's: they are drawn and
    // they take clicks, so a shape that describes only the window has to
    // have them added back or they would be cut away with everything else
    // outside it.
    QRegion shape = QRegion(rect()) - QRegion(picture_rect());
    for (const QRect& rect : guest_region_) {
        shape += where.logical_rect(rect);
    }
    if (shape != mask()) setMask(shape);
}

// The guest made this its foreground window itself — a dialog that
// opened, a window an application raised — and the mirror asks to be the
// active one here. The compositor decides, as it does for any window
// asking; one whose user is elsewhere is refused.
void MirrorWindow::follow_guest_foreground(bool foreground) {
    if (!foreground || isActiveWindow() || !style_.activates()) return;
    if (parentWidget() != nullptr) return;
    activateWindow();
}

Qt::WindowFlags MirrorWindow::flags_for(const Win32Style& style) const noexcept {
    // Qt::ToolTip is the one kind Qt maps to an xdg_popup without a grab:
    // placed by its offset from the parent surface, and left up until the
    // guest takes it down. Qt::Popup would place it too, but grabs the
    // pointer and dismisses itself on a click outside, and the guest is
    // the one that decides when its window is done.
    const Qt::WindowType type =
        parentWidget() != nullptr ? Qt::ToolTip : style.toplevel_type();
    return type | style.hints();
}

void MirrorWindow::wear(const Win32Style& style) {
    // Showing it must not pull host focus off whatever the user is in.
    //
    // A popup never asks for it either, whatever the guest window's own
    // styles say. It is a surface of its owner, which already has the
    // focus, and a window that asks this compositor for activation it was
    // given no token for is a window flashing for attention — once for
    // every menu opened.
    setAttribute(Qt::WA_ShowWithoutActivating,
                 !style.activates() || parentWidget() != nullptr);
    // WS_DISABLED is not setEnabled(false). Qt hands a disabled widget's
    // state down to its children, and a mirror's children are the popups
    // of that window — so a main window disabled behind its own modal
    // dialog took the dialog's mirror down with it, and nothing in it
    // could be clicked. Windows disables the one window and no other.
    // What it means is that the window takes no input from the user,
    // which is done where the input arrives.
    apply_size_limits();
}

// A sizable mirror is held at what the guest has room for, so a drag
// stops where the guest window would have stopped.
//
// Except maximised: the compositor gives a maximised window the size of
// the screen and a maximum that refuses it leaves the window part
// maximised and part not. There the mirror takes the whole screen and the
// guest's is fitted inside it.
void MirrorWindow::apply_size_limits() {
    if (!style_.sizable()) {
        setFixedSize(logical_size());
        return;
    }
    setMinimumSize(QSize{0, 0});
    const bool held = !maximized_ && guest_room_.isValid() && !guest_room_.isEmpty();
    // Rounded up like any guest size, so a window as large as the room is
    // shown whole. The pixel or so beyond it the guest would be asked for
    // at that size is cut back to the room by the guest itself.
    setMaximumSize(held ? PixelMapping{devicePixelRatioF()}.logical_size(guest_room_)
                        : QSize{QWIDGETSIZE_MAX, QWIDGETSIZE_MAX});
}

void MirrorWindow::follow_guest_style(const GuestWindowInfo& window) {
    take_shape(window.region);
    const Win32Style style{window};
    if (style == style_) return;
    style_ = style;
    const Qt::WindowFlags flags = flags_for(style);
    // New flags mean a new surface, made from scratch: the old one keeps
    // its role, and a role change on a live surface is a protocol error.
    const bool remap = flags != windowFlags() && isVisible();
    if (flags != windowFlags()) {
        destroy();
        setWindowFlags(flags);
    }
    wear(style);
    if (remap) show();
    RWIN_DEBUG("win32", "{:#x} restyled: sizable={} activates={} taskbar={} topmost={} "
               "disabled={}",
               handle_, style.sizable(), style.activates(), style.in_taskbar(),
               style.topmost(), style.disabled());
}

void MirrorWindow::present(const QImage& pixels) {
    if (!style_.color_keyed()) {
        frame_ = pixels;
        update();
        return;
    }
    // The keyed colour is transparent wherever the window painted it.
    // Done into a buffer of the mirror's own, kept at the frame's size,
    // so a keyed window costs one pass over its pixels and no allocation
    // once it has been seen at that size.
    if (keyed_.size() != pixels.size() || keyed_.format() != pixels.format()) {
        keyed_ = QImage(pixels.size(), pixels.format());
    }
    const QRgb key = qRgb(style_.color_key() & 0xFF, (style_.color_key() >> 8) & 0xFF,
                          (style_.color_key() >> 16) & 0xFF);
    for (int y = 0; y < pixels.height(); ++y) {
        const auto* from = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        auto* to = reinterpret_cast<QRgb*>(keyed_.scanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            to[x] = (from[x] & 0x00FFFFFF) == (key & 0x00FFFFFF) ? 0 : from[x];
        }
    }
    frame_ = keyed_;
    update();
}

void MirrorWindow::retitle(const QString& title) {
    if (!title.isEmpty() && title != windowTitle()) {
        setWindowTitle(title);
    }
}

void MirrorWindow::guest_closed() {
    guest_closed_ = true;
    close();
}

bool MirrorWindow::holds_press() const noexcept {
    if (held_ != Qt::NoButton) return true;
    for (const MirrorWindow* popup : findChildren<MirrorWindow*>(Qt::FindDirectChildrenOnly)) {
        if (popup->holds_press()) return true;
    }
    return false;
}

void MirrorWindow::close_when_released() {
    guest_closed_ = true;
    closing_when_released_ = true;
    frame_ = QImage();
    update();
    RWIN_DEBUG("input", "{:#x} gone in the guest but holding a press; staying until release",
               handle_);
}

void MirrorWindow::wear_guest_cursor(const QCursor& cursor) {
    guest_cursor_ = cursor;
    // Not while the pointer is in the resize band: there the shape has to
    // say the edge can be dragged, and the guest has no idea that band
    // exists.
    if (hovered_edges_ == Qt::Edges{}) {
        setCursor(dressed(guest_cursor_));
    }
}

// The guest drew its cursor in its own pixels, which are this screen's
// device pixels. Told nothing, Qt takes each for a logical unit and the
// compositor scales the cursor up a second time.
//
// Qt's Wayland cursor takes only a whole buffer scale (6.11: the ratio
// truncated, the hotspot scaled up to match). On a screen at 2 this is a
// cursor drawn one to one; at 1.2 it is drawn at scale 1 and stretched by
// the compositor, as it was before, with its hotspot still in place.
QCursor MirrorWindow::dressed(const QCursor& cursor) const {
    QPixmap pixels = cursor.pixmap();
    if (pixels.isNull()) return cursor;
    const double ratio = devicePixelRatioF();
    pixels.setDevicePixelRatio(ratio);
    const QPoint hotspot = cursor.hotSpot();
    return QCursor(pixels, static_cast<int>(std::lround(hotspot.x() / ratio)),
                   static_cast<int>(std::lround(hotspot.y() / ratio)));
}

void MirrorWindow::take_over_drag() {
    QWindow* surface = windowHandle();
    if (surface == nullptr) return;
    swallow_release_ = true;
    if (!surface->startSystemMove()) {
        RWIN_WARN("chrome", "{:#x} compositor refused the drag", handle_);
        swallow_release_ = false;
    }
}

void MirrorWindow::follow_guest_state(bool minimized, bool maximized) {
    // A popup has no state of its own: it shows and hides with its parent.
    if (parentWidget() != nullptr) return;

    if (minimized != minimized_) {
        minimized_ = minimized;
        applying_guest_state_ = true;
        if (minimized) {
            showMinimized();
        } else {
            showNormal();
            raise();
        }
        applying_guest_state_ = false;
        RWIN_DEBUG("chrome", "{:#x} follows guest minimized={}", handle_, minimized);
    }

    // The guest window was maximised from its own chrome — the button in
    // the pixels, which is the one the user sees. The mirror covers this
    // screen for it, and the guest's screen is fitted into that.
    if (maximized && !maximized_) {
        maximized_by_guest_ = true;
        applying_guest_state_ = true;
        showMaximized();
        applying_guest_state_ = false;
        RWIN_DEBUG("chrome", "{:#x} maximised because the guest window was", handle_);
        return;
    }
    // Restored from that same chrome. A mirror maximised here instead
    // stays as it is: the window it shows got smaller, and it is shown
    // smaller, in the middle of the screen the user gave it.
    if (!maximized && maximized_ && maximized_by_guest_) {
        applying_guest_state_ = true;
        showNormal();
        applying_guest_state_ = false;
        RWIN_DEBUG("chrome", "{:#x} restored because the guest window was", handle_);
    }
}

void MirrorWindow::place_under(MirrorWindow& owner, QPoint guest_offset) {
    guest_offset_ = guest_offset;
    // Where the owner draws the guest's pixels, plus the offset at the
    // scale they are drawn at: a popup lands on the part of the picture
    // the guest opened it against, whatever size that picture is here.
    // Rounded to a whole logical unit, which is all a popup is placed by:
    // within half a unit, a device pixel at the scales anyone runs.
    const QPoint at = owner.pos() + owner.mapping().logical_point(guest_offset).toPoint();
    if (parentWidget() != &owner) {
        // Re-parenting hides the widget, and gives it the parent surface
        // its popup is positioned against. The native window goes first:
        // a Wayland surface keeps the role it was given, and asking the
        // one that was a toplevel to be a popup is a protocol error that
        // ends the process.
        const bool shown = isVisible();
        destroy();
        setParent(&owner, Qt::ToolTip | style_.hints());
        setAttribute(Qt::WA_ShowWithoutActivating);
        move(at);
        if (shown) show();
        RWIN_DEBUG("popup", "{:#x} now a popup of {:#x}", handle_, owner.handle());
    }
    // A shown popup moving is xdg_popup.reposition: this is how a window
    // the guest dragged follows on this side.
    move(at);
}

// The guest answered a request of ours, or sized its own window.
void MirrorWindow::settle_request(QSize reported) {
    if (!awaiting_.isValid()) return;
    // Any answer ends the wait: the guest applies what it is sent in
    // order, so a report is the end of the last request whatever size it
    // came out as. Waiting for the exact size asked for would wait
    // forever the first time the guest gave a different one.
    awaiting_ = QSize();
    if (!pending_.isValid()) return;
    const QSize next = pending_;
    pending_ = QSize();
    if (next != reported) ask_guest_for(next);
}

void MirrorWindow::note_guest_size(std::uint32_t width, std::uint32_t height) {
    const QSize reported(static_cast<int>(width), static_cast<int>(height));
    settle_request(reported);
    if (reported == guest_size_) {
        // The window is the size it was, which is not to say the mirror
        // is: the scale it is drawn at may have changed under it.
        if (parentWidget() != nullptr) follow_root_scale();
        return;
    }
    guest_size_ = reported;

    if (parentWidget() != nullptr) {
        follow_root_scale();
        return;
    }

    // A maximised mirror keeps the size the compositor gave it; the guest
    // window is the guest's whole screen, and it is fitted into that.
    if (maximized_ || isFullScreen()) {
        refit();
        return;
    }
    if (const QSize wanted = logical_size(); size() != wanted) {
        applying_guest_size_ = true;
        // A window without a sizing border is pinned here, so plain
        // resize() does nothing to it. It still has to follow the guest:
        // what a window draws is not what its rectangle claims, and the
        // mirror is made before the first frame says which. Pinned at the
        // wrong size, the frame is centred inside it and the difference
        // shows as a border.
        if (style_.sizable()) {
            resize(wanted);
        } else {
            setFixedSize(wanted);
        }
        applying_guest_size_ = false;
    }
    refit();
}

// A popup's own screen ratio, because that is what its buffer is made at;
// its toplevel's fit, because a menu beside a picture half its size is a
// menu in the wrong place.
PixelMapping MirrorWindow::mapping() const {
    return PixelMapping{devicePixelRatioF(), root().fit_}.centring(picture_size(), size());
}

// The frame when there is one: it is what is drawn, and between a resize
// and the guest's answer it is not the size the guest last reported.
QSize MirrorWindow::picture_size() const {
    return frame_.isNull() ? guest_size_ : frame_.size();
}

QRect MirrorWindow::picture_rect() const {
    return mapping().logical_rect(QRect(QPoint(0, 0), picture_size()));
}

// The guest window's size in this desktop's logical units. A popup is
// drawn at its toplevel's fit. The toplevel itself is one to one: its fit
// follows from the size it ends up, not the other way round.
QSize MirrorWindow::logical_size() const {
    const double fit = parentWidget() != nullptr ? root().fit_ : 1.0;
    return PixelMapping{devicePixelRatioF(), fit}.logical_size(guest_size_);
}

// The fit of the guest window into this mirror. One whenever the mirror is
// the size of that window, which the guest is asked for and answers with —
// so this is not 1 only where the mirror cannot be that size: maximised,
// against a screen the guest's own does not match.
void MirrorWindow::refit() {
    if (parentWidget() != nullptr) return;
    const double fitted = PixelMapping{devicePixelRatioF()}.fit_for(guest_size_, size());
    if (fitted == fit_) return;
    fit_ = fitted;
    RWIN_DEBUG("mirror", "{:#x} {}x{} at ratio {:.3f} shows {}x{} guest pixels at fit {:.3f}",
               handle_, width(), height(), devicePixelRatioF(), guest_size_.width(),
               guest_size_.height(), fit_);
    // Every window under this one, not only those parented straight to
    // it: a menu opened from a panel is a popup of a popup.
    apply_shape();
    for (MirrorWindow* popup : findChildren<MirrorWindow*>()) {
        popup->follow_root_scale();
    }
    update();
}

// A popup is the size its own guest window is, at its owner's scale.
void MirrorWindow::follow_root_scale() {
    const QSize wanted = logical_size();
    if (wanted.isEmpty() || wanted == size()) {
        update();
        return;
    }
    applying_guest_size_ = true;
    if (style_.sizable()) {
        resize(wanted);
    } else {
        setFixedSize(wanted);
    }
    applying_guest_size_ = false;
    apply_shape();
}

void MirrorWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    // Source, not SourceOver: the frame replaces what is under it,
    // transparency and all. Blending would composite the guest's
    // see-through pixels onto whatever the widget last held.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    // A layered window's constant alpha: in Source mode the opacity
    // scales what is written, so the whole window comes out that
    // translucent, guest pixels' own alpha included.
    painter.setOpacity(style_.opacity() / 255.0);
    if (frame_.isNull()) {
        painter.fillRect(rect(), Qt::transparent);
        return;
    }
    const PixelMapping where = mapping();
    if (picture_rect() != rect()) {
        // The bars beside a fitted picture: black, and opaque, because
        // they are this mirror's own and not a part of the window that
        // happens to be see-through. The picture is drawn over the middle
        // of them, and its own transparent pixels replace the black
        // rather than blending with it, Source mode being what is set.
        painter.fillRect(rect(), Qt::black);
    }
    draw_picture(painter, frame_, where);
}

bool MirrorWindow::event(QEvent* event) {
    const bool handled = QWidget::event(event);
    // Delivered to event() and not changeEvent(), and sometimes twice:
    // following it is idempotent.
    if (event->type() == QEvent::DevicePixelRatioChange) follow_device_pixel_ratio();
    return handled;
}

// On a screen of another scale - or shown at all: until the compositor
// names a surface's scale, Qt gives it its screen's, which on a
// fractionally scaled desktop is the scale rounded up to a whole one.
//
// The guest window keeps its pixels, and the mirror changes its logical
// size to go on showing them one to one. Keeping the logical size instead
// would mean asking the guest for as many pixels as that is on the new
// screen: an application laid out for one DPI given a different number of
// pixels at the same DPI is cut short or padded, not rescaled. So on a
// screen less dense than the guest's DPI was set for, a mirror is larger
// than a native window would be, and still sharp.
void MirrorWindow::follow_device_pixel_ratio() {
    RWIN_DEBUG("mirror", "{:#x} device pixel ratio {:.3f}", handle_, devicePixelRatioF());
    apply_size_limits();
    if (hovered_edges_ == Qt::Edges{}) setCursor(dressed(guest_cursor_));
    if (parentWidget() != nullptr) {
        follow_root_scale();
    } else {
        if (!maximized_ && !isFullScreen() && style_.sizable()) {
            applying_guest_size_ = true;
            resize(logical_size());
            applying_guest_size_ = false;
        }
        refit();
        apply_shape();
        update();
    }
    // Every popup's place against this window is in this window's logical
    // units, which have just changed under it.
    for (MirrorWindow* popup : findChildren<MirrorWindow*>(Qt::FindDirectChildrenOnly)) {
        popup->place_under(*this, popup->guest_offset_);
    }
}

void MirrorWindow::closeEvent(QCloseEvent* event) {
    if (guest_closed_) {
        event->accept();
        return;
    }
    // CS_NOCLOSE: Windows would not act on this, so neither does the
    // mirror. The guest is not asked, and the window stays.
    if (!style_.closable()) {
        RWIN_DEBUG("win32", "{:#x} does not close: CS_NOCLOSE", handle_);
        event->ignore();
        return;
    }
    link_.send_close(handle_);
    event->accept();
}

void MirrorWindow::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    // Injected input goes to whatever the guest has in front. Focusing a
    // mirror here has to move it there, or the keystrokes land in
    // whichever guest window was last touched.
    link_.send_focus(handle_);
}

void MirrorWindow::limit_to(QSize guest_room) {
    if (!guest_room.isValid() || guest_room.isEmpty() || guest_room == guest_room_) return;
    guest_room_ = guest_room;
    apply_size_limits();
    if (maximized_) ask_guest_for(guest_room_);
}

// Maximised here, and the guest window is given the whole of the guest's
// screen — the largest it can be, and never a maximise of its own: a
// guest window maximised is sized to a screen this mirror is not on, and
// the two disagreed about what its size meant. What comes back is fitted
// into the mirror, letterboxed where the two screens are not the same
// shape.
void MirrorWindow::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange || parentWidget() != nullptr) return;
    const bool maximized = isMaximized();
    if (maximized == maximized_) return;
    maximized_ = maximized;
    apply_size_limits();
    RWIN_DEBUG("chrome", "{:#x} maximised={} here (the guest's doing: {})", handle_,
               maximized_, maximized_by_guest_);
    // The guest maximised itself and this followed: it is already as
    // large as its screen lets it be, and asking for a size would take it
    // back out of maximised.
    if (maximized_ && maximized_by_guest_) return;
    if (!maximized_) maximized_by_guest_ = false;
    const QSize wanted = maximized_ && guest_room_.isValid() && !guest_room_.isEmpty()
                             ? guest_room_
                             : PixelMapping{devicePixelRatioF()}.guest_size(size());
    if (!wanted.isEmpty() && wanted != guest_size_) ask_guest_for(wanted);
}

void MirrorWindow::ask_guest_for(QSize size) {
    if (size.isEmpty()) return;
    awaiting_ = size;
    RWIN_DEBUG("mirror", "{:#x} resized to {}x{} guest pixels", handle_, size.width(),
               size.height());
    link_.send_resize(handle_, static_cast<std::uint32_t>(size.width()),
                      static_cast<std::uint32_t>(size.height()));
}

void MirrorWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (parentWidget() != nullptr) return;
    // Whatever the mirror's size came from, the guest's window has to be
    // fitted into it again — and its shape put where the picture now is.
    refit();
    apply_shape();
    if (applying_guest_size_ || applying_guest_state_ || !isVisible()) return;
    // A maximised mirror's size is the screen's, and says nothing about
    // how big the guest window should be; that was settled when it was
    // maximised, and it is the guest's whole screen.
    if (maximized_) return;
    // Compared in logical units: the guest size a mirror holds is not
    // always the size it was made for (1001 pixels at 1.2 is 835 units,
    // and 835 units hold 1002), and asking for it would grow the window a
    // pixel at a time.
    if (event->size() == logical_size()) return;
    const QSize wanted = PixelMapping{devicePixelRatioF()}.guest_size(event->size());
    if (awaiting_.isValid()) {
        pending_ = wanted;
        return;
    }
    ask_guest_for(wanted);
}

const MirrorWindow& MirrorWindow::root() const {
    const MirrorWindow* window = this;
    while (const auto* owner = qobject_cast<const MirrorWindow*>(window->parentWidget())) {
        window = owner;
    }
    return *window;
}

// Pointer positions are told in the frame of the toplevel this window
// descends from, never in the frame of a popup.
//
// A popup sits where the guest said it was a frame ago. While the guest
// drags one, the pointer here is measured against a window that is
// already behind it, and the guest then measures that against where the
// window is now, so the drag is fed its own lag and shakes. And the
// window that took the press may not be the window under the release:
// Photoshop hides a panel the moment it is picked up, and a release told
// in the frame of a window that is gone reaches nothing. The toplevel
// does neither.
//
// A point is dropped only when it is outside this window's own picture: on
// its bars, or in the sliver under a logical unit that rounding leaves past
// its last pixel. Measured against the toplevel's picture, a menu hanging
// below its window took no clicks on the part that hung below.
std::optional<QPoint> MirrorWindow::guest_position_of(QMouseEvent* event) const {
    const QPoint own = mapping().guest_point(event->position());
    if (!QRect(QPoint(0, 0), picture_size()).contains(own)) return std::nullopt;
    return guest_origin() + own;
}

QPoint MirrorWindow::guest_origin() const {
    const auto* owner = qobject_cast<const MirrorWindow*>(parentWidget());
    if (owner == nullptr) return QPoint(0, 0);
    return owner->guest_origin() + guest_offset_;
}

// The host's own band, which the guest cannot report: a window's resize
// border is outside its visible frame, so no point inside a mirror is in
// it. Only a window with a sizing border has one.
Qt::Edges MirrorWindow::edges_at(QPointF position) const noexcept {
    if (!style_.sizable() || style_.disabled() || isMaximized()) return Qt::Edges{};
    return border_.at(position, size());
}

void MirrorWindow::mouseMoveEvent(QMouseEvent* event) {
    if (style_.disabled()) return;
    const Qt::Edges edges = edges_at(event->position());
    if (edges != hovered_edges_) {
        hovered_edges_ = edges;
        setCursor(edges == Qt::Edges{} ? dressed(guest_cursor_)
                                       : QCursor(ResizeBorder::cursor_for(edges)));
    }
    // Forwarded even over the chrome and the border: that is how the guest
    // updates its own hover highlighting and decides which cursor to show.
    const std::optional<QPoint> at = guest_position_of(event);
    if (!at.has_value()) return;
    link_.send_pointer_move(root().handle(), at->x(), at->y());
}

bool MirrorWindow::start_edge_resize(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;
    const Qt::Edges edges = edges_at(event->position());
    if (edges == Qt::Edges{}) return false;

    QWindow* surface = windowHandle();
    if (surface == nullptr) return false;
    RWIN_DEBUG("chrome", "{:#x} resize from edges {:#x}", handle_,
               static_cast<unsigned>(edges.toInt()));
    // A popup the compositor will not resize falls through: the press
    // reaches the guest, in the guest window's own sizing border, and
    // Windows runs the resize the mirror then follows.
    swallow_release_ = surface->startSystemResize(edges);
    return swallow_release_;
}

void MirrorWindow::mousePressEvent(QMouseEvent* event) {
    // WS_DISABLED: "A disabled window cannot receive input from the user."
    if (style_.disabled()) return;
    // A release still owed from a drag the compositor took over never
    // comes: the compositor kept it. A new press means that wait is over,
    // or this press's own release would be the one swallowed.
    swallow_release_ = false;
    if (start_edge_resize(event)) {
        event->accept();
        return;
    }
    const std::optional<QPoint> at = guest_position_of(event);
    if (!at.has_value()) return;
    held_ |= event->button();
    link_.send_pointer_button(root().handle(), at->x(), at->y(),
                              button_index(event->button()), true);
}

void MirrorWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (style_.disabled()) return;
    held_ &= ~event->button();
    if (swallow_release_) {
        swallow_release_ = false;
        return;
    }
    const std::optional<QPoint> at = guest_position_of(event);
    if (at.has_value()) {
        link_.send_pointer_button(root().handle(), at->x(), at->y(),
                                  button_index(event->button()), false);
    }
    if (held_ == Qt::NoButton) Q_EMIT let_go();
}

void MirrorWindow::send_key(QKeyEvent* event, bool pressed) {
    if (style_.disabled()) return;
    const std::uint32_t virtual_key = virtual_key_for(event->key());
    const QString text = event->text();
    const std::uint32_t character =
        (virtual_key == 0 && !text.isEmpty() && text.at(0).unicode() >= 0x20)
            ? text.at(0).unicode()
            : 0;
    if (virtual_key == 0 && character == 0) return;
    link_.send_key(handle_, virtual_key, character, pressed);
}

void MirrorWindow::keyPressEvent(QKeyEvent* event) { send_key(event, true); }

void MirrorWindow::keyReleaseEvent(QKeyEvent* event) { send_key(event, false); }

}  // namespace rwin::host
