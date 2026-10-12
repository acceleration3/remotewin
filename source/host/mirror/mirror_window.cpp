#include "host/mirror/mirror_window.hpp"

#include "host/cursor/cursor_look.hpp"
#include "host/input/guest_button.hpp"
#include "host/export/shared_frame.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/draw_picture.hpp"
#include "host/mirror/gpu_picture.hpp"
#include "host/mirror/pixel_mapping.hpp"
#include "logging/logger.hpp"
#include "proto/pointer_at.hpp"
#include "proto/pointer_wheel.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QCursor>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QPixmap>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>

#include <cmath>
#include <utility>

namespace rwin::host {

namespace {

// How soon after a show or a minimise a focus-in is the compositor's own:
// a round trip, with room.
constexpr qint64 kMapFocusMs = 750;

}  // namespace

MirrorWindow::MirrorWindow(GuestLink& link, const GuestWindowInfo& window,
                           MirrorWindow* owner, QString title_tag, PixelCursor& pixel_cursor)
    : QWidget(owner),
      link_(link),
      handle_(window.handle),
      title_tag_(std::move(title_tag)),
      style_(window),
      keys_(QGuiApplication::platformName()),
      guest_size_(static_cast<int>(window.width), static_cast<int>(window.height)),
      guest_foreground_(window.foreground),
      worn_(pixel_cursor) {
    setWindowFlags(flags_for(style_));
    setWindowTitle((window.title.isEmpty() ? QStringLiteral("Guest window") : window.title)
                   + title_tag_);
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
    // Shown activating is asking to be the active window.
    if (!testAttribute(Qt::WA_ShowWithoutActivating)) ++activation_requests_;
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
//
// Asked once, when the guest's foreground becomes this window: a refusal is
// the compositor's answer, and asking again with every window list is a
// window nagging for attention.
void MirrorWindow::follow_guest_foreground(bool foreground) {
    const bool became = foreground && !guest_foreground_;
    guest_foreground_ = foreground;
    if (!became || isActiveWindow() || !style_.activates() || is_popup()) return;
    setAttribute(Qt::WA_ShowWithoutActivating, false);
    ++activation_requests_;
    activateWindow();
}

void MirrorWindow::reclaim_activation() {
    if (isActiveWindow() || !style_.activates() || is_popup()) return;
    ++activation_requests_;
    activateWindow();
}

void MirrorWindow::come_forward() {
    if (is_popup()) {
        if (auto* owner = qobject_cast<MirrorWindow*>(parentWidget())) owner->come_forward();
        return;
    }
    raise();
    if (style_.activates() && !isActiveWindow()) {
        ++activation_requests_;
        activateWindow();
    }
    ++alerts_;
    QApplication::alert(this);
    RWIN_DEBUG("win32", "{:#x} brought forward for its disabled owner", handle_);
}

void MirrorWindow::attach_to(MirrorWindow* owner) {
    if (!is_popup() && attached_to_ == owner) return;
    RWIN_DEBUG("popup", "{:#x} now a window of its own, attached to {:#x}", handle_,
               owner != nullptr ? owner->handle() : 0);
    attached_to_ = owner;
    remake_surface();
}

void MirrorWindow::remake_surface() {
    const bool shown = isVisible();
    destroy();
    // Not flags_for: while it is still a child it would say popup, and a
    // popup with no parent is one Qt closes.
    setParent(nullptr, style_.toplevel_type() | style_.hints());
    wear(style_);
    // A popup was sized at its owner's fit; a toplevel is one to one.
    applying_guest_size_ = true;
    if (style_.sizable()) {
        resize(logical_size());
    } else {
        setFixedSize(logical_size());
    }
    applying_guest_size_ = false;
    refit();
    apply_shape();
    create();
    if (QWindow* surface = windowHandle()) {
        surface->setTransientParent(attached_to_.isNull() ? nullptr : attached_to_->windowHandle());
    }
    if (!shown) return;
    show();
    if (!testAttribute(Qt::WA_ShowWithoutActivating)) ++activation_requests_;
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
    //
    // Nor does a window the guest did not make its foreground: one shown
    // with SW_SHOWNOACTIVATE, a palette that opens beside the window in
    // use. Activated here, it would be focused, and focusing a mirror
    // moves the guest's foreground to it - the mirror would have changed
    // the guest's activation rather than followed it.
    setAttribute(Qt::WA_ShowWithoutActivating,
                 !style.activates() || is_popup() || !guest_foreground_);
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

MirrorState MirrorWindow::state() const {
    MirrorState state;
    state.handle = handle_;
    if (const auto* owner = qobject_cast<const MirrorWindow*>(parentWidget())) {
        state.popup_of = owner->handle();
    }
    if (!attached_to_.isNull()) state.attached_to = attached_to_->handle();
    const auto* owner = !attached_to_.isNull() ? attached_to_.data()
                                               : qobject_cast<const MirrorWindow*>(parentWidget());
    state.modal = owner != nullptr && owner->style_.disabled() && !style_.disabled();
    state.activation_requests = activation_requests_;
    state.alerts = alerts_;
    state.flags = windowFlags();
    state.shows_without_activating = testAttribute(Qt::WA_ShowWithoutActivating);
    state.visible = isVisible();
    state.minimized = minimized_ || isMinimized();
    state.maximized = isMaximized();
    state.active = isActiveWindow();
    state.forwards_input = !style_.disabled();
    state.resizable = minimumSize() != maximumSize();
    state.title = windowTitle().chopped(title_tag_.size());
    state.position = pos();
    state.placed = parentWidget() != nullptr ? placed_ : pos();
    state.logical_size = size();
    state.device_pixel_ratio = devicePixelRatioF();
    state.fit = root().fit_;
    state.guest_size = guest_size_;
    const QRectF picture = mapping().picture(picture_size());
    const double ratio = devicePixelRatioF();
    state.picture = QRectF(picture.topLeft() * ratio, picture.size() * ratio).toRect();
    state.opacity = style_.opacity();
    state.color_keyed = style_.color_keyed();
    state.color_key = style_.color_key();
    state.mask = mask();
    describe_cursor(state);
    state.frames_presented = frames_presented_;
    return state;
}

QImage MirrorWindow::grab_shown() {
    QImage shown = grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!shows_gpu_frame()) return shown;
    QImage picture = gpu_->drawn().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    shown.setDevicePixelRatio(1.0);
    QPainter painter{&shown};
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawImage(QPoint(0, 0), picture);
    painter.end();
    shown.setDevicePixelRatio(devicePixelRatioF());
    return shown;
}

void MirrorWindow::take_gpu_frames() {
    if (gpu_ != nullptr) return;
    gpu_ = new GpuPicture();
    gpu_view_ = QWidget::createWindowContainer(gpu_, this);
    gpu_view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    gpu_view_->setFocusPolicy(Qt::NoFocus);
    gpu_view_->setGeometry(rect());
    gpu_view_->hide();
    connect(gpu_, &GpuPicture::failed, this, &MirrorWindow::gpu_failed);
    connect(gpu_, &GpuPicture::frame_timed, this, &MirrorWindow::frame_timed);
}

// A popup's window is made again here, the first time: one is placed
// against its owner however often that is.
void MirrorWindow::present_shared(const SharedFrame& frame) {
    if (guest_closed_) return;
    take_gpu_frames();
    ++frames_presented_;
    const bool was_shown = shows_gpu_frame();
    const QSize before = picture_size();
    frame_ = QImage();
    gpu_->show_frame(frame);
    gpu_->lay_out(gpu_layout());
    if (gpu_view_->isHidden()) gpu_view_->show();
    // This window's own pixels are painted again only when what they are
    // changes - the pixel path's picture gone, or the bars round another
    // size: painted every frame, the whole window would be sent to the GPU
    // again with each.
    if (!was_shown || before != picture_size()) update();
}

bool MirrorWindow::shows_gpu_frame() const {
    return gpu_ != nullptr && frame_.isNull() && gpu_->has_frame();
}

// The picture's place, scale and look, worked out as paintEvent works them
// out for QPainter. The shape is the mask's own rectangles in device pixels,
// rounded outward as the mask is: what the pixel path's painting is clipped
// to.
PictureLayout MirrorWindow::gpu_layout() const {
    const PixelMapping where = mapping();
    PictureLayout layout;
    layout.picture = where.device_picture(picture_size());
    layout.smooth = !where.one_to_one();
    layout.opacity = style_.opacity() / 255.0;
    if (style_.color_keyed()) {
        layout.color_key = qRgb(style_.color_key() & 0xFF, (style_.color_key() >> 8) & 0xFF,
                                (style_.color_key() >> 16) & 0xFF);
    }
    if (!guest_region_.isEmpty()) {
        const double ratio = devicePixelRatioF();
        for (const QRect& unit : mask()) {
            layout.shape += QRect(QPoint(static_cast<int>(std::floor(unit.left() * ratio)),
                                         static_cast<int>(std::floor(unit.top() * ratio))),
                                  QPoint(static_cast<int>(std::ceil((unit.right() + 1) * ratio)) - 1,
                                         static_cast<int>(std::ceil((unit.bottom() + 1) * ratio)) - 1));
        }
    }
    layout.bars = picture_rect() != rect();
    return layout;
}

void MirrorWindow::present(const QImage& pixels) {
    ++frames_presented_;
    if (gpu_ != nullptr && !gpu_view_->isHidden()) {
        gpu_->forget();
        gpu_view_->hide();
    }
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
    if (!title.isEmpty() && title + title_tag_ != windowTitle()) {
        setWindowTitle(title + title_tag_);
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
    if (gpu_ != nullptr) gpu_->forget();
    update();
    RWIN_DEBUG("input", "{:#x} gone in the guest but holding a press; staying until release",
               handle_);
}

void MirrorWindow::wear_guest_cursor(const CursorLook* look, std::size_t step) {
    worn_.wear(look, step);
    show_guest_cursor();
}

// Not while the pointer is in the resize band: there the shape has to say
// the edge can be dragged, and the guest has no idea that band exists.
void MirrorWindow::show_guest_cursor() {
    if (hovered_edges_ != Qt::Edges{}) return;
    worn_.put_on(*this);
}

void MirrorWindow::describe_cursor(MirrorState& state) const {
    const QCursor worn = cursor();
    state.cursor_shape = worn.shape();
    state.hovered_edges = hovered_edges_;
    const CursorLook* look = worn_.look();
    state.cursor_id = look != nullptr ? look->id : 0;
    if (worn.shape() == Qt::BitmapCursor) {
        state.cursor_pixels = worn.pixmap().toImage();
        state.cursor_pixmap_ratio = worn.pixmap().devicePixelRatio();
        state.cursor_hotspot = worn.hotSpot();
    }
    if (look == nullptr) return;
    state.cursor_inverts = look->inverts;
    state.cursor_delays_ms.reserve(look->frames.size());
    for (const CursorLook::Frame& frame : look->frames) state.cursor_delays_ms.push_back(frame.delay_ms);
}

QPointF MirrorWindow::logical_point_of(QPoint guest) const { return mapping().logical_point(guest); }

QRect MirrorWindow::logical_rect_inside(QRect guest) const { return mapping().logical_rect_inside(guest); }

void MirrorWindow::take_over_drag() {
    QWindow* surface = windowHandle();
    if (surface == nullptr) return;
    swallow_release_ = true;
    if (!surface->startSystemMove()) {
        RWIN_WARN("chrome", "{:#x} compositor refused the drag", handle_);
        swallow_release_ = false;
        return;
    }
    // The compositor owns the release now; it need not reach this widget.
    // Keeping the press here leaves a blank mirror alive after the guest hides it.
    held_ = Qt::NoButton;
    held_at_.reset();
    Q_EMIT let_go();
}

void MirrorWindow::follow_guest_state(bool minimized, bool maximized) {
    // IsZoomed is false for a minimised window, and Windows restores it
    // maximised all the same: while it is minimised, whether it is
    // maximised stays what it was.
    if (!minimized) guest_maximized_ = maximized;
    // A popup has no state of its own: it shows and hides with its parent.
    if (parentWidget() != nullptr) return;

    if (minimized != minimized_) {
        minimized_ = minimized;
        applying_guest_state_ = true;
        changed_for_guest_.start();
        touched_ = false;
        if (minimized) {
            showMinimized();
        } else {
            unminimize();
            raise();
        }
        applying_guest_state_ = false;
        RWIN_DEBUG("chrome", "{:#x} follows guest minimized={}", handle_, minimized);
    }
    if (minimized) return;

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

void MirrorWindow::unminimize() {
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
}

void MirrorWindow::place_under(MirrorWindow& owner, QPoint guest_offset) {
    guest_offset_ = guest_offset;
    // Where the owner draws the guest's pixels, plus the offset at the
    // scale they are drawn at: a popup lands on the part of the picture
    // the guest opened it against, whatever size that picture is here.
    // Rounded to a whole logical unit, which is all a popup is placed by:
    // within half a unit, a device pixel at the scales anyone runs.
    const QPoint at = owner.pos() + owner.mapping().logical_point(guest_offset).toPoint();
    placed_ = at;
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
    if (!frame_.isNull()) return frame_.size();
    return shows_gpu_frame() ? gpu_->picture_size() : guest_size_;
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
    // Drawn by the GpuPicture over all of this, which Qt leaves a hole in
    // the window's own pixels for.
    if (shows_gpu_frame()) gpu_->lay_out(gpu_layout());
    if (frame_.isNull()) {
        painter.fillRect(rect(), Qt::transparent);
        return;
    }
    const PixelMapping where = mapping();
    // The bars beside a fitted picture: black, and opaque, because they
    // are this mirror's own and not a part of the window that happens to
    // be see-through. Around the picture, never under it: Source with an
    // opacity below one is a blend of the picture with what is beneath,
    // and a layered window maximised over black bars came out darker
    // everywhere.
    if (picture_rect() != rect()) fill_bars(painter, where.picture(frame_.size()));
    // A layered window's constant alpha, on a buffer the bars leave
    // transparent under the picture: the whole window comes out that
    // translucent, guest pixels' own alpha included.
    painter.setOpacity(style_.opacity() / 255.0);
    draw_picture(painter, frame_, where);
}

// Above, below, left and right of the picture, each clamped: a picture
// larger than the mirror while the guest catches up has no bar there.
void MirrorWindow::fill_bars(QPainter& painter, const QRectF& picture) const {
    const QRectF whole(rect());
    const auto bar = [&](double left, double top, double width, double height) {
        if (width > 0.0 && height > 0.0) painter.fillRect(QRectF(left, top, width, height), Qt::black);
    };
    bar(whole.left(), whole.top(), whole.width(), picture.top() - whole.top());
    bar(whole.left(), picture.bottom(), whole.width(), whole.bottom() - picture.bottom());
    bar(whole.left(), picture.top(), picture.left() - whole.left(), picture.height());
    bar(picture.right(), picture.top(), whole.right() - picture.right(), picture.height());
}

bool MirrorWindow::event(QEvent* event) {
    const bool handled = QWidget::event(event);
    // Delivered to event() and not changeEvent(), and sometimes twice:
    // following it is idempotent.
    if (event->type() == QEvent::DevicePixelRatioChange) follow_device_pixel_ratio();
    if (event->type() == QEvent::WinIdChange) ++surfaces_;
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
    show_guest_cursor();
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
    RWIN_DEBUG("win32", "{:#x} closed here; asking the guest to close it", handle_);
    link_.send_close(handle_);
    event->accept();
}

// Put on again as the pointer comes in: a pixel cursor's surface is sized
// for the ratio of the window it was last put on, and this one's may differ.
void MirrorWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    if (worn_.look() != nullptr) show_guest_cursor();
}

void MirrorWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    changed_for_guest_.start();
    touched_ = false;
}

// KWin activates a window as it maps it when its client is the active one,
// whatever the window asked (WA_ShowWithoutActivating is no part of
// xdg-shell). That focus is not the user's choice, and passing it on would
// have the mirror move the guest's foreground rather than follow it.
void MirrorWindow::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (!guest_foreground_ && !touched_ && changed_for_guest_.isValid()
        && !changed_for_guest_.hasExpired(kMapFocusMs)) {
        RWIN_DEBUG("focus", "{:#x} focused by the compositor as it changed it; not the guest's", handle_);
        Q_EMIT focused_unasked(handle_);
        return;
    }
    // Injected input goes to whatever the guest has in front. Focusing a
    // mirror here has to move it there, or the keystrokes land in
    // whichever guest window was last touched.
    link_.send_focus(handle_);
}

void MirrorWindow::limit_to(QSize guest_room) {
    if (!guest_room.isValid() || guest_room.isEmpty() || guest_room == guest_room_) return;
    guest_room_ = guest_room;
    apply_size_limits();
    // Maximised here, the guest window is given the guest's whole room;
    // maximised there, it has it already.
    if (maximized_ && !maximized_by_guest_ && !guest_maximized_) ask_guest_for(guest_room_);
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
    // Nor when the guest restored itself and this followed: the mirror is
    // still the maximised size as it leaves maximised, and asking for that
    // would give the restored window the whole screen, at its corner.
    const bool restored_by_guest = !maximized_ && maximized_by_guest_;
    if (!maximized_) maximized_by_guest_ = false;
    if (restored_by_guest) return;
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
    if (gpu_view_ != nullptr) gpu_view_->setGeometry(rect());
    if (parentWidget() != nullptr) return;
    // Whatever the mirror's size came from, the guest's window has to be
    // fitted into it again — and its shape put where the picture now is.
    refit();
    apply_shape();
    if (applying_guest_size_ || applying_guest_state_ || !isVisible()) return;
    // Sized before it was ever on screen: the compositor's first answer to
    // a window it has not placed yet, which no user asked for.
    if (windowHandle() == nullptr || !windowHandle()->isExposed()) return;
    // A maximised mirror's size is the screen's, and says nothing about
    // how big the guest window should be; that was settled when it was
    // maximised, and it is the guest's whole screen. A window maximised or
    // minimised in the guest is not given a size either: Windows would
    // have to restore it first, and the mirror would have undone what the
    // guest did.
    if (maximized_ || guest_maximized_ || minimized_) return;
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

// Wherever the pointer is, in the guest window's pixels, outside it too: a
// press the guest has seen holds the mouse capture there, and Windows goes
// on telling the window where the pointer is until the button comes up.
// The compositor keeps sending this surface the pointer for as long as the
// button is held (its implicit grab), so these are the only moves the guest
// can have - a drag out of a narrow tool panel is all outside it.
QPoint MirrorWindow::guest_point_of(QMouseEvent* event) const {
    return guest_origin() + mapping().guest_point(event->position());
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
        if (edges == Qt::Edges{}) {
            show_guest_cursor();
        } else {
            setCursor(ResizeBorder::cursor_for(edges));
        }
    }
    // Forwarded even over the chrome and the border: that is how the guest
    // updates its own hover highlighting and decides which cursor to show.
    // Held, the pointer's own motion moves the guest's (held_motion): a
    // point against this surface is against where the compositor has put
    // it, which while Windows drags the window is a step behind it.
    if (held_at_.has_value()) return;
    const std::optional<QPoint> at =
        held_ != Qt::NoButton ? std::optional(guest_point_of(event)) : guest_position_of(event);
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
    touched_ = true;
    // WS_DISABLED: "A disabled window cannot receive input from the user."
    if (style_.disabled()) {
        Q_EMIT pressed_while_disabled(handle_);
        return;
    }
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
                              guest_button(event->button()), true);
    if (relative_motion_ && !held_at_.has_value()) {
        held_at_ = QPointF(root().guest_position_ + *at);
        RWIN_DEBUG("pointer", "{:#x} held at {},{} on the guest's screen (window at {},{})", handle_,
                   held_at_->x(), held_at_->y(), root().guest_position_.x(), root().guest_position_.y());
    }
}

// Logical units of this surface to the guest's pixels: device pixels per
// unit, over device pixels per guest pixel.
void MirrorWindow::held_motion(QPointF logical) {
    if (!held_at_.has_value()) return;
    *held_at_ += logical * (devicePixelRatioF() / root().fit_);
    RWIN_TRACE("pointer", "{:#x} held, moved {:.2f},{:.2f} to {:.1f},{:.1f}", handle_, logical.x(), logical.y(),
               held_at_->x(), held_at_->y());
    link_.send_pointer_at(PointerAt{root().handle(), qRound(held_at_->x()), qRound(held_at_->y()),
                                    PointerAt::kNoButton, false});
}

void MirrorWindow::mouseReleaseEvent(QMouseEvent* event) {
    const bool pressed_here = (held_ & event->button()) != 0;
    held_ &= ~event->button();
    if (held_ == Qt::NoButton) Q_EMIT let_go();
    // A release the guest saw the press of goes to it wherever it happens,
    // and whatever the window has become since: left out, the guest holds
    // the button down for good, and whatever it was doing with it - a drag,
    // with its overlay disabling the window under it - never ends.
    if (style_.disabled() && !pressed_here) return;
    if (swallow_release_) {
        swallow_release_ = false;
        return;
    }
    if (pressed_here && held_at_.has_value()) {
        link_.send_pointer_at(PointerAt{root().handle(), qRound(held_at_->x()), qRound(held_at_->y()),
                                        guest_button(event->button()), false});
        if (held_ == Qt::NoButton) held_at_.reset();
        return;
    }
    const std::optional<QPoint> at =
        pressed_here ? std::optional(guest_point_of(event)) : guest_position_of(event);
    if (at.has_value()) {
        link_.send_pointer_button(root().handle(), at->x(), at->y(),
                                  guest_button(event->button()), false);
    }
}

// Qt's angle delta is eighths of a degree, 120 a notch, which is Windows'
// own WHEEL_DELTA; a horizontal one is positive to the left, Windows'
// to the right.
void MirrorWindow::wheelEvent(QWheelEvent* event) {
    touched_ = true;
    if (style_.disabled()) return;
    const QPoint own = mapping().guest_point(event->position());
    if (!QRect(QPoint(0, 0), picture_size()).contains(own)) return;
    const QPoint at = guest_origin() + own;
    link_.send_pointer_wheel(PointerWheel{root().handle(), at.x(), at.y(), -event->angleDelta().x(),
                                          event->angleDelta().y()});
    event->accept();
}

void MirrorWindow::send_key(QKeyEvent* event, bool pressed) {
    touched_ = true;
    if (style_.disabled()) return;
    // A held key repeats as a press and a release each time here; Windows
    // repeats it as presses alone.
    if (!pressed && event->isAutoRepeat()) return;
    const std::optional<KeyStroke> stroke = keys_.stroke_for(*event, handle_, pressed);
    if (!stroke.has_value()) return;
    held_keys_.note(*stroke);
    link_.send_key(*stroke);
}

void MirrorWindow::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    held_keys_.release_all(link_, handle_);
}

// Tab is the guest window's key. QWidget takes it to move the focus to
// the next widget first, and a mirror is a widget alone, which the focus
// "moves" to - and the key is gone.
bool MirrorWindow::focusNextPrevChild(bool /*next*/) { return false; }

void MirrorWindow::keyPressEvent(QKeyEvent* event) { send_key(event, true); }

void MirrorWindow::keyReleaseEvent(QKeyEvent* event) { send_key(event, false); }

}  // namespace rwin::host
