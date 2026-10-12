#include "host/mirror/monitor_window.hpp"

#include "host/export/shared_frame.hpp"
#include "host/input/guest_button.hpp"
#include "host/link/guest_link.hpp"
#include "host/mirror/draw_picture.hpp"
#include "host/mirror/gpu_picture.hpp"
#include "host/mirror/picture_layout.hpp"
#include "host/mirror/pixel_mapping.hpp"
#include "logging/logger.hpp"
#include "proto/key_stroke.hpp"
#include "proto/message_id.hpp"
#include "proto/pointer_wheel.hpp"

#include <QCloseEvent>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QRect>
#include <QScreen>

#include <algorithm>

namespace rwin::host {

namespace {

// Before the guest's screen size is known.
constexpr QSize kWaitingSize{960, 600};
// Of this desktop's free screen, at most: room for the decoration and for
// whatever else is on the screen.
constexpr double kMostOfScreen = 0.9;
// The buttons the guest has a number for (guest_button).
constexpr Qt::MouseButtons kGuestButtons = Qt::LeftButton | Qt::MiddleButton | Qt::RightButton;

}  // namespace

MonitorWindow::MonitorWindow(GuestLink& link, const QString& title, PixelCursor& pixel_cursor,
                             QWidget* parent)
    : QWidget(parent), link_(link), keys_(QGuiApplication::platformName()), worn_(pixel_cursor) {
    setWindowTitle(title);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    resize(kWaitingSize);
}

void MonitorWindow::note_guest_connected() {
    guest_connected_ = true;
    update();
}

void MonitorWindow::set_interactive(bool interactive) {
    interactive_ = interactive;
    setFocusPolicy(interactive_ ? Qt::StrongFocus : Qt::NoFocus);
    setMouseTracking(interactive_);
    setAttribute(Qt::WA_TransparentForMouseEvents, !interactive_);
    if (!interactive_) {
        held_keys_.release_all(link_, kMonitorWindow);
        clearFocus();
        unsetCursor();
    }
}

void MonitorWindow::present(const QImage& pixels) {
    const QSize before = picture_size();
    if (gpu_ != nullptr && !gpu_view_->isHidden()) {
        gpu_->forget();
        gpu_view_->hide();
    }
    frame_ = pixels;
    note_picture(before);
    update();
}

void MonitorWindow::take_gpu_frames() {
    if (gpu_ != nullptr) return;
    gpu_ = new GpuPicture();
    gpu_view_ = QWidget::createWindowContainer(gpu_, this);
    gpu_view_->setAttribute(Qt::WA_TransparentForMouseEvents);
    gpu_view_->setFocusPolicy(Qt::NoFocus);
    gpu_view_->setGeometry(rect());
    gpu_view_->hide();
    connect(gpu_, &GpuPicture::failed, this, &MonitorWindow::gpu_failed);
    connect(gpu_, &GpuPicture::frame_timed, this, &MonitorWindow::frame_timed);
}

void MonitorWindow::present_shared(const SharedFrame& frame) {
    if (gpu_ == nullptr) return;
    const QSize before = picture_size();
    frame_ = QImage();
    gpu_->show_frame(frame);
    note_picture(before);
    lay_out_gpu();
    if (gpu_view_->isHidden()) gpu_view_->show();
}

void MonitorWindow::note_picture(QSize before) {
    const QSize now = picture_size();
    if (now == before) return;
    RWIN_INFO("monitor", "the guest's screen is {}x{}", now.width(), now.height());
    if (before.isEmpty() && isWindow()) size_to(now);
}

bool MonitorWindow::shows_gpu_frame() const { return gpu_ != nullptr && frame_.isNull() && gpu_->has_frame(); }

QSize MonitorWindow::picture_size() const {
    if (!frame_.isNull()) return frame_.size();
    return shows_gpu_frame() ? gpu_->picture_size() : QSize();
}

void MonitorWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (gpu_view_ == nullptr) return;
    gpu_view_->setGeometry(rect());
    lay_out_gpu();
}

// The picture with black all round it: this window is the guest's screen,
// and nothing of it is see-through.
void MonitorWindow::lay_out_gpu() {
    if (!shows_gpu_frame()) return;
    const PixelMapping where = mapping();
    PictureLayout layout;
    layout.picture = where.device_picture(picture_size());
    layout.smooth = !where.one_to_one();
    layout.bars = true;
    gpu_->lay_out(layout);
}

void MonitorWindow::patch(QSize picture, QPoint at, const QImage& pixels) {
    if (frame_.isNull() || frame_.size() != picture) return;
    {
        QPainter painter(&frame_);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(at, pixels);
    }
    update(mapping().logical_rect(QRect(at, pixels.size())));
}

void MonitorWindow::size_to(QSize guest) {
    const QSize natural = PixelMapping{devicePixelRatioF()}.logical_size(guest);
    const QScreen* on = screen();
    if (on == nullptr) {
        resize(natural);
        return;
    }
    const QSize room = on->availableGeometry().size() * kMostOfScreen;
    resize(natural.width() <= room.width() && natural.height() <= room.height()
               ? natural
               : natural.scaled(room, Qt::KeepAspectRatio));
}

void MonitorWindow::wear_guest_cursor(const CursorLook* look, std::size_t step) {
    if (!interactive_) return;
    worn_.wear(look, step);
    worn_.put_on(*this);
}

PixelMapping MonitorWindow::mapping() const {
    const double ratio = devicePixelRatioF();
    const double fit = std::min(1.0, PixelMapping{ratio}.fit_for(picture_size(), size()));
    return PixelMapping{ratio, fit}.centring(picture_size(), size());
}

std::optional<QPoint> MonitorWindow::guest_point_of(QPointF position) const {
    if (picture_size().isEmpty()) return std::nullopt;
    const QPoint guest = mapping().guest_point(position);
    if (!QRect(QPoint(0, 0), picture_size()).contains(guest)) return std::nullopt;
    return guest;
}

void MonitorWindow::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    if (shows_gpu_frame()) return;
    if (frame_.isNull()) {
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter,
                         guest_connected_ ? QStringLiteral("Waiting for the guest's first frame")
                                          : QStringLiteral("Waiting for the guest to connect"));
        return;
    }
    draw_picture(painter, frame_, mapping());
}

bool MonitorWindow::event(QEvent* event) {
    const bool handled = QWidget::event(event);
    // Another scale is another one to one, and a cursor picture sized for
    // the old one.
    if (event->type() == QEvent::DevicePixelRatioChange) {
        worn_.put_on(*this);
        lay_out_gpu();
        update();
    }
    return handled;
}

void MonitorWindow::closeEvent(QCloseEvent* event) {
    event->accept();
    Q_EMIT closed();
}

// Put on again as the pointer comes in: a pixel cursor's surface is sized
// for the ratio of the window it was last put on.
void MonitorWindow::enterEvent(QEnterEvent* event) {
    QWidget::enterEvent(event);
    if (interactive_) worn_.put_on(*this);
}

void MonitorWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!interactive_) return;
    const std::optional<QPoint> at = guest_point_of(event->position());
    if (at.has_value()) link_.send_pointer_move(kMonitorWindow, at->x(), at->y());
}

void MonitorWindow::mousePressEvent(QMouseEvent* event) { send_button(event, true); }

void MonitorWindow::mouseReleaseEvent(QMouseEvent* event) { send_button(event, false); }

// A press on a bar is this desktop's, and reaches nothing. A release is
// sent wherever it is, held to the picture's edge: the guest holds the
// button until it hears one.
void MonitorWindow::send_button(QMouseEvent* event, bool pressed) {
    if (!interactive_) return;
    const QSize picture = picture_size();
    if ((event->button() & kGuestButtons) == 0 || picture.isEmpty()) return;
    std::optional<QPoint> at = guest_point_of(event->position());
    if (!at.has_value()) {
        if (pressed) return;
        const QPoint outside = mapping().guest_point(event->position());
        at = QPoint(std::clamp(outside.x(), 0, picture.width() - 1),
                    std::clamp(outside.y(), 0, picture.height() - 1));
    }
    link_.send_pointer_button(kMonitorWindow, at->x(), at->y(), guest_button(event->button()),
                              pressed);
}

// In Windows' wheel units, as MirrorWindow::wheelEvent sends them.
void MonitorWindow::wheelEvent(QWheelEvent* event) {
    if (!interactive_) return;
    const std::optional<QPoint> at = guest_point_of(event->position());
    if (!at.has_value()) return;
    link_.send_pointer_wheel(PointerWheel{kMonitorWindow, at->x(), at->y(), -event->angleDelta().x(),
                                          event->angleDelta().y()});
    event->accept();
}

void MonitorWindow::send_key(QKeyEvent* event, bool pressed) {
    if (!interactive_) return;
    // A held key repeats as a press and a release each time here; Windows
    // repeats it as presses alone.
    if (!pressed && event->isAutoRepeat()) return;
    const std::optional<KeyStroke> stroke = keys_.stroke_for(*event, kMonitorWindow, pressed);
    if (!stroke.has_value()) return;
    held_keys_.note(*stroke);
    link_.send_key(*stroke);
}

void MonitorWindow::keyPressEvent(QKeyEvent* event) { send_key(event, true); }

void MonitorWindow::keyReleaseEvent(QKeyEvent* event) { send_key(event, false); }

void MonitorWindow::focusOutEvent(QFocusEvent* event) {
    QWidget::focusOutEvent(event);
    held_keys_.release_all(link_, kMonitorWindow);
}

// Tab is the guest's key. QWidget takes it to move the focus to the next
// widget first, and this window is a widget alone.
bool MonitorWindow::focusNextPrevChild(bool next) {
    return interactive_ ? false : QWidget::focusNextPrevChild(next);
}

}  // namespace rwin::host
