#pragma once

#include "host/guest_window_info.hpp"

#include <QImage>
#include <QSize>
#include <QWidget>

namespace rwin::host {

class GuestLink;

// One guest window, shown as a window of this desktop.
//
// The frame is the guest's client pixels; the frame *around* it is the
// host compositor's, asked for by kind rather than painted from a copy of
// the guest's chrome. Input taken here goes back to the guest window it
// came from.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    MirrorWindow(GuestLink& link, const GuestWindowInfo& window,
                 QWidget* parent = nullptr);

    [[nodiscard]] std::uint64_t handle() const noexcept { return handle_; }

    void present(const QImage& pixels);
    void retitle(const QString& title);

    // The size the guest window actually has, as the last enumeration
    // reported it. Applied to the widget without being echoed back.
    void note_guest_size(std::uint32_t width, std::uint32_t height);

    // The guest window is verifiably gone: close without asking the guest
    // to close a window that no longer exists.
    void guest_closed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void send_button(QMouseEvent* event, bool pressed);
    void send_key(QKeyEvent* event, bool pressed);

    GuestLink& link_;
    std::uint64_t handle_;
    QImage frame_;
    QSize guest_size_;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
};

}  // namespace rwin::host
