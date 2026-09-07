#include "host/mirror/mirror_window.hpp"

#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"

#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QResizeEvent>
#include <QWindow>

#include <algorithm>

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
    resize(guest_size_);
    wear(style_);
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
void MirrorWindow::take_shape(const QList<QRect>& region) {
    if (region.isEmpty()) {
        if (!mask().isEmpty()) clearMask();
        return;
    }
    QRegion shape;
    for (const QRect& rect : region) shape += rect;
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
    setAttribute(Qt::WA_ShowWithoutActivating, !style.activates());
    setEnabled(!style.disabled());
    if (style.sizable()) {
        setMinimumSize(QSize{0, 0});
        setMaximumSize(QSize{QWIDGETSIZE_MAX, QWIDGETSIZE_MAX});
    } else {
        setFixedSize(guest_size_);
    }
}

void MirrorWindow::follow_guest_style(const GuestWindowInfo& window) {
    take_shape(window.region);
    const Win32Style style{window};
    if (style == style_) return;
    style_ = style;
    const Qt::WindowFlags flags = flags_for(style);
    // New flags mean a new surface; Qt hides the widget to make one.
    const bool remap = flags != windowFlags() && isVisible();
    if (flags != windowFlags()) setWindowFlags(flags);
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

void MirrorWindow::wear_guest_cursor(const QCursor& cursor) {
    guest_cursor_ = cursor;
    // Not while the pointer is in the resize band: there the shape has to
    // say the edge can be dragged, and the guest has no idea that band
    // exists.
    if (hovered_edges_ == Qt::Edges{}) {
        setCursor(guest_cursor_);
    }
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
    if (minimized == isMinimized() && maximized == isMaximized()) return;
    applying_guest_state_ = true;
    if (minimized) {
        showMinimized();
    } else if (maximized) {
        showMaximized();
    } else {
        showNormal();
    }
    applying_guest_state_ = false;
    RWIN_DEBUG("chrome", "{:#x} follows guest state minimized={} maximized={}", handle_,
               minimized, maximized);
}

void MirrorWindow::place_under(MirrorWindow& owner, QPoint guest_offset) {
    if (parentWidget() != &owner) {
        // Re-parenting hides the widget, and gives it the parent surface
        // its popup is positioned against.
        const bool shown = isVisible();
        setParent(&owner, Qt::ToolTip | style_.hints());
        move(owner.pos() + guest_offset);
        if (shown) show();
        RWIN_DEBUG("popup", "{:#x} now a popup of {:#x}", handle_, owner.handle());
    }
    // A shown popup moving is xdg_popup.reposition: this is how a window
    // the guest dragged follows on this side.
    move(owner.pos() + guest_offset);
}

void MirrorWindow::note_guest_size(std::uint32_t width, std::uint32_t height) {
    const QSize reported(static_cast<int>(width), static_cast<int>(height));
    if (reported == guest_size_) return;
    guest_size_ = reported;
    // A maximised mirror keeps the size the compositor gave it. The guest
    // screen may be smaller than this one, so the window it manages to
    // reach is smaller than the mirror — following it here would shrink
    // the mirror back out of maximised the moment it got there.
    if (isMaximized() || isFullScreen()) {
        update();
        return;
    }
    if (size() == reported) return;
    applying_guest_size_ = true;
    // A window without a sizing border is pinned here, so plain resize()
    // does nothing to it. It still has to follow the guest: what a window
    // draws is not what its rectangle claims, and the mirror is made
    // before the first frame says which. Pinned at the wrong size, the
    // frame is centred inside it and the difference shows as a border.
    if (style_.sizable()) {
        resize(reported);
    } else {
        setFixedSize(reported);
    }
    applying_guest_size_ = false;
}

QPoint MirrorWindow::frame_origin() const {
    return QPoint(std::max(0, (width() - frame_.width()) / 2),
                  std::max(0, (height() - frame_.height()) / 2));
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
    const QPoint origin = frame_origin();
    if (origin != QPoint(0, 0)) {
        painter.fillRect(rect(), Qt::transparent);
    }
    painter.drawImage(origin, frame_);
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

void MirrorWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (applying_guest_size_ || applying_guest_state_ || !isVisible()) return;
    const QSize wanted = event->size();
    if (wanted == guest_size_) return;
    guest_size_ = wanted;
    RWIN_DEBUG("mirror", "{:#x} resized to {}x{} guest pixels", handle_, wanted.width(),
               wanted.height());
    link_.send_resize(handle_, static_cast<std::uint32_t>(wanted.width()),
                      static_cast<std::uint32_t>(wanted.height()));
}

QPoint MirrorWindow::guest_position_of(QMouseEvent* event) const {
    const QPointF at = event->position() - QPointF(frame_origin());
    return QPoint(static_cast<int>(at.x()), static_cast<int>(at.y()));
}

// The host's own band, which the guest cannot report: a window's resize
// border is outside its visible frame, so no point inside a mirror is in
// it. Only a window with a sizing border has one.
Qt::Edges MirrorWindow::edges_at(QPointF position) const noexcept {
    if (!style_.sizable() || isMaximized()) return Qt::Edges{};
    return border_.at(position, size());
}

void MirrorWindow::mouseMoveEvent(QMouseEvent* event) {
    const Qt::Edges edges = edges_at(event->position());
    if (edges != hovered_edges_) {
        hovered_edges_ = edges;
        setCursor(edges == Qt::Edges{} ? guest_cursor_
                                       : QCursor(ResizeBorder::cursor_for(edges)));
    }
    // Forwarded even over the chrome and the border: that is how the guest
    // updates its own hover highlighting and decides which cursor to show.
    const QPoint at = guest_position_of(event);
    link_.send_pointer_move(handle_, at.x(), at.y());
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
    if (start_edge_resize(event)) {
        event->accept();
        return;
    }
    const QPoint at = guest_position_of(event);
    link_.send_pointer_button(handle_, at.x(), at.y(), button_index(event->button()),
                              true);
}

void MirrorWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (swallow_release_) {
        swallow_release_ = false;
        return;
    }
    const QPoint at = guest_position_of(event);
    link_.send_pointer_button(handle_, at.x(), at.y(), button_index(event->button()),
                              false);
}

void MirrorWindow::send_key(QKeyEvent* event, bool pressed) {
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
