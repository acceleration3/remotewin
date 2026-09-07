#include "host/mirror_window.hpp"

#include "host/guest_link.hpp"
#include "host/window_style.hpp"

#include "log/logger.hpp"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>

namespace rwin::host {

namespace {

// Only the keys an Edit control cannot infer from a character: everything
// printable arrives as text and goes as WM_CHAR.
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

MirrorWindow::MirrorWindow(GuestLink& link, const GuestWindowInfo& window, QWidget* parent)
    : QWidget(parent),
      link_(link),
      handle_(window.handle),
      guest_size_(static_cast<int>(window.width), static_cast<int>(window.height)) {
    const WindowStyle style{window.class_name.toStdString(), window.style,
                            window.extended_style};
    setWindowFlags(style.window_flags());
    setWindowTitle(window.title.isEmpty() ? QStringLiteral("Guest window")
                                          : window.title);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    RWIN_DEBUG("style", "{:#x} -> menu={} dialog={} popup={} resizable={} flags={:#x}",
               handle_, style.is_menu(), style.is_dialog(), style.is_popup(),
               style.is_resizable(),
               static_cast<unsigned>(style.window_flags().toInt()));
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

void MirrorWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    if (frame_.isNull()) {
        painter.fillRect(rect(), palette().window());
        return;
    }
    painter.drawImage(QPoint(0, 0), frame_);
}

void MirrorWindow::closeEvent(QCloseEvent* event) {
    // Closing the mirror IS closing the guest window; the mirror goes away
    // when the guest confirms the window is gone.
    if (!guest_closed_) {
        link_.send_close(handle_);
    }
    event->accept();
}

void MirrorWindow::note_guest_size(std::uint32_t width, std::uint32_t height) {
    const QSize reported(static_cast<int>(width), static_cast<int>(height));
    if (reported == guest_size_) return;
    guest_size_ = reported;
    if (size() == reported) return;
    // The guest resized itself. Follow it, but do not tell it what it
    // already knows — that is the echo that had the two of them growing
    // each other a few pixels at a time.
    applying_guest_size_ = true;
    resize(reported);
    applying_guest_size_ = false;
}

void MirrorWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (applying_guest_size_ || event->size() == guest_size_) {
        return;
    }
    // Qt lays a widget out before it is on screen; a size from that is
    // this process talking to itself, not the user dragging an edge.
    if (!isVisible()) {
        return;
    }
    guest_size_ = event->size();
    RWIN_DEBUG("mirror", "{:#x} resized to {}x{}; telling the guest", handle_,
               event->size().width(), event->size().height());
    link_.send_resize(handle_, static_cast<std::uint32_t>(event->size().width()),
                      static_cast<std::uint32_t>(event->size().height()));
}

void MirrorWindow::mouseMoveEvent(QMouseEvent* event) {
    link_.send_pointer_move(handle_, static_cast<std::int32_t>(event->position().x()),
                            static_cast<std::int32_t>(event->position().y()));
}

void MirrorWindow::send_button(QMouseEvent* event, bool pressed) {
    link_.send_pointer_button(handle_, static_cast<std::int32_t>(event->position().x()),
                              static_cast<std::int32_t>(event->position().y()),
                              button_index(event->button()), pressed);
}

void MirrorWindow::mousePressEvent(QMouseEvent* event) { send_button(event, true); }

void MirrorWindow::mouseReleaseEvent(QMouseEvent* event) { send_button(event, false); }

void MirrorWindow::send_key(QKeyEvent* event, bool pressed) {
    const std::uint32_t virtual_key = virtual_key_for(event->key());
    const QString text = event->text();
    const std::uint32_t character =
        (!text.isEmpty() && text.at(0).unicode() >= 0x20) ? text.at(0).unicode() : 0;
    if (virtual_key == 0 && character == 0) return;
    link_.send_key(handle_, virtual_key, character, pressed);
}

void MirrorWindow::keyPressEvent(QKeyEvent* event) { send_key(event, true); }

void MirrorWindow::keyReleaseEvent(QKeyEvent* event) { send_key(event, false); }

}  // namespace rwin::host
