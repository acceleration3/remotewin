#pragma once

#include "host/chrome/non_client_area.hpp"
#include "host/link/guest_window_info.hpp"

#include <QImage>
#include <QSize>
#include <QWidget>

namespace rwin::host {

class GuestLink;

// One guest window, shown as a window of this desktop.
//
// Frameless: what looks like a title bar is the guest's own, arriving in
// the pixels. Pressing it does not go back to the guest — NonClientArea
// says which part of its chrome the pointer is over, and a press there
// moves, resizes, minimises, maximises or closes THIS window instead. The
// guest keeps drawing its chrome; the host keeps owning what it means.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    MirrorWindow(GuestLink& link, const GuestWindowInfo& window,
                 QWidget* parent = nullptr);

    [[nodiscard]] std::uint64_t handle() const noexcept { return handle_; }

    void present(const QImage& pixels);
    void retitle(const QString& title);
    void note_guest_size(std::uint32_t width, std::uint32_t height);
    void note_hit(std::int32_t hit_code) noexcept { hit_ = NonClientArea{hit_code}; }

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
    void focusInEvent(QFocusEvent* event) override;

private:
    // True when the press was the guest's chrome asking for something this
    // window does itself.
    [[nodiscard]] bool act_on_chrome(QMouseEvent* event);
    void send_button(QMouseEvent* event, bool pressed);
    void send_key(QKeyEvent* event, bool pressed);

    GuestLink& link_;
    std::uint64_t handle_;
    QImage frame_;
    QSize guest_size_;
    NonClientArea hit_;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
};

}  // namespace rwin::host
