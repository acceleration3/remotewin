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

// Only the keys a character cannot express: everything printable arrives
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
                            window.extended_style};
    setWindowFlags(style.window_flags());
    setWindowTitle(window.title.isEmpty() ? QStringLiteral("Guest window")
                                          : window.title);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    RWIN_DEBUG("style", "{:#x} -> menu={} dialog={} popup={} resizable={}", handle_,
               style.is_menu(), style.is_dialog(), style.is_popup(),
               style.is_resizable());
    resize(static_cast<int>(window.width), static_cast<int>(window.height));
    if (!style.is_resizable()) {
        setFixedSize(size());
    }
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

void MirrorWindow::place_under(MirrorWindow& owner, QPoint guest_offset) {
    // Both surfaces have to exist before the link between them can be
    // made, and it has to be made before either maps.
    owner.createWinId();
    createWinId();
    QWindow* mine = windowHandle();
    QWindow* theirs = owner.windowHandle();
    if (mine == nullptr || theirs == nullptr) return;
    mine->setTransientParent(theirs);
    move(owner.pos() + guest_offset);
    RWIN_DEBUG("popup", "{:#x} placed under {:#x} at offset {},{}", handle_,
               owner.handle(), guest_offset.x(), guest_offset.y());
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

void MirrorWindow::note_guest_size(std::uint32_t width, std::uint32_t height) {
    const QSize reported(static_cast<int>(width), static_cast<int>(height));
    if (reported == guest_size_) return;
    guest_size_ = reported;
    if (size() == reported) return;
    // A maximised mirror keeps the size the compositor gave it. The guest
    // screen may be smaller than this one, so the window it manages to
    // reach is smaller than the mirror — following it here would shrink
    // the mirror back out of maximised the moment it got there.
    if (isMaximized() || isFullScreen()) {
        update();
        return;
    }
    // The guest resized itself. Follow it, but do not tell it what it
    // already knows — that is the echo that had the two of them growing
    // each other a few pixels at a time.
    applying_guest_size_ = true;
    resize(reported);
    applying_guest_size_ = false;
}

QPoint MirrorWindow::frame_origin() const {
    return QPoint(std::max(0, (width() - frame_.width()) / 2),
                  std::max(0, (height() - frame_.height()) / 2));
}

void MirrorWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    if (frame_.isNull()) {
        painter.fillRect(rect(), palette().window());
        return;
    }
    const QPoint origin = frame_origin();
    if (origin != QPoint(0, 0)) {
        painter.fillRect(rect(), palette().window());
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
    if (applying_guest_size_ || event->size() == guest_size_ || !isVisible()) {
        return;
    }
    guest_size_ = event->size();
    link_.send_resize(handle_, static_cast<std::uint32_t>(event->size().width()),
                      static_cast<std::uint32_t>(event->size().height()));
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
    const QPoint origin = frame_origin();
    link_.send_pointer_move(
        handle_, static_cast<std::int32_t>(event->position().x() - origin.x()),
        static_cast<std::int32_t>(event->position().y() - origin.y()));
}

bool MirrorWindow::act_on_chrome(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return false;

    QWindow* surface = windowHandle();
    // The host's own band first: the guest cannot report an edge that is
    // outside its visible frame, which all of them are.
    if (const Qt::Edges edges = border_.at(event->position(), size());
        edges != Qt::Edges{} && !isMaximized()) {
        RWIN_DEBUG("chrome", "{:#x} resize from edges {:#x}", handle_,
                   static_cast<unsigned>(edges.toInt()));
        return surface != nullptr && surface->startSystemResize(edges);
    }
    if (!hit_.is_host_action()) return false;
    RWIN_DEBUG("chrome", "{:#x} press in zone {}", handle_, hit_.code());

    if (hit_.is_close()) {
        close();
        return true;
    }
    if (hit_.is_minimize()) {
        showMinimized();
        return true;
    }
    if (hit_.is_maximize()) {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
        return true;
    }
    if (surface == nullptr) return false;
    // Whatever the guest called an edge is inside its frame — a sizing
    // grip, say — so honour it as a resize too.
    if (const Qt::Edges edges = hit_.resize_edges(); edges != Qt::Edges{}) {
        return surface->startSystemResize(edges);
    }
    return surface->startSystemMove();
}

void MirrorWindow::send_button(QMouseEvent* event, bool pressed) {
    const QPoint origin = frame_origin();
    link_.send_pointer_button(
        handle_, static_cast<std::int32_t>(event->position().x() - origin.x()),
        static_cast<std::int32_t>(event->position().y() - origin.y()),
        button_index(event->button()), pressed);
}

void MirrorWindow::mousePressEvent(QMouseEvent* event) {
    if (act_on_chrome(event)) {
        event->accept();
        return;
    }
    send_button(event, true);
}

void MirrorWindow::mouseReleaseEvent(QMouseEvent* event) { send_button(event, false); }

void MirrorWindow::send_key(QKeyEvent* event, bool pressed) {
    const std::uint32_t virtual_key = virtual_key_for(event->key());
    const QString text = event->text();
    const std::uint32_t character =
        (virtual_key == 0 && !text.isEmpty() && text.at(0).unicode() >= 0x20)
            ? text.at(0).unicode()
            : 0;
    if (virtual_key == 0 && character == 0) return;
    RWIN_DEBUG("input", "{:#x} key {} vk={:#x} char={:#x}", handle_,
               pressed ? "down" : "up", virtual_key, character);
    link_.send_key(handle_, virtual_key, character, pressed);
}

void MirrorWindow::keyPressEvent(QKeyEvent* event) { send_key(event, true); }

void MirrorWindow::keyReleaseEvent(QKeyEvent* event) { send_key(event, false); }

}  // namespace rwin::host
