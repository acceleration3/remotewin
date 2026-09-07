#include "host/mirror/mirror_window.hpp"

#include "host/chrome/window_style.hpp"
#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"

#include <QCloseEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
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
                           QWidget* parent)
    : QWidget(parent),
      link_(link),
      handle_(window.handle),
      owner_(window.owner),
      guest_size_(static_cast<int>(window.width), static_cast<int>(window.height)) {
    const WindowStyle style{window.class_name.toStdString(), window.style,
                            window.extended_style, window.owner != 0};
    setWindowFlags(style.window_flags());
    setWindowTitle(window.title.isEmpty() ? QStringLiteral("Guest window")
                                          : window.title);
    setAttribute(Qt::WA_DeleteOnClose);
    // The guest's pixels carry their own alpha. Without this the widget
    // paints an opaque background first and every transparent part of the
    // window — a rounded corner, a shadow, a see-through panel — lands on
    // top of it.
    setAttribute(Qt::WA_TranslucentBackground);
    if (style.is_popup()) {
        // Opening a menu must not pull host focus off the window it
        // belongs to, the way a real menu does not.
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    resize(guest_size_);
    if (!style.is_resizable()) {
        setFixedSize(size());
    }
    RWIN_DEBUG("style", "{:#x} {}x{} popup={} dialog={} menu={}", handle_, window.width,
               window.height, style.is_popup(), style.is_dialog(), style.is_menu());
}

void MirrorWindow::present(const QImage& pixels) {
    frame_ = pixels;
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
    const QPoint offset = guest_offset;
    // Only before it maps: re-parenting a shown widget hides it, and a
    // Wayland popup's parent is fixed when its surface is created anyway.
    if (!isVisible() && parentWidget() != &owner) {
        setParent(&owner, windowFlags());
    }
    move(owner.pos() + offset);
    RWIN_DEBUG("popup", "{:#x} under {:#x} at offset {},{} (owner at {},{})", handle_,
               owner.handle(), offset.x(), offset.y(), owner.pos().x(), owner.pos().y());
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
    // A window the guest will not let be resized is pinned here, so plain
    // resize() does nothing to it. It still has to follow the guest: what
    // a window draws is not what its rectangle claims, and the mirror is
    // made before the first frame says which. Pinned at the wrong size,
    // the frame is centred inside it and the difference shows as a border.
    if (minimumSize() == maximumSize()) {
        setFixedSize(reported);
    } else {
        resize(reported);
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
    if (!guest_closed_) {
        link_.send_close(handle_);
    }
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

void MirrorWindow::mouseMoveEvent(QMouseEvent* event) {
    const Qt::Edges edges = border_.at(event->position(), size());
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

// The host's own band, which the guest cannot report: a window's resize
// border is outside its visible frame, so no point inside a mirror is in
// it.
bool MirrorWindow::start_edge_resize(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || isMaximized()) return false;
    const Qt::Edges edges = border_.at(event->position(), size());
    if (edges == Qt::Edges{}) return false;

    QWindow* surface = windowHandle();
    if (surface == nullptr) return false;
    RWIN_DEBUG("chrome", "{:#x} resize from edges {:#x}", handle_,
               static_cast<unsigned>(edges.toInt()));
    swallow_release_ = true;
    return surface->startSystemResize(edges);
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
