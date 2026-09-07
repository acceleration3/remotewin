#pragma once

#include "host/chrome/resize_border.hpp"
#include "host/link/guest_window_info.hpp"

#include <QCursor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

namespace rwin::host {

class GuestLink;

// One guest window, shown as a window of this desktop.
//
// Frameless: what looks like a title bar is the guest's own, arriving in
// the pixels. Nothing about that chrome is interpreted here — a press on
// it is forwarded, and if Windows decides it began a drag the guest says
// so and the drag is taken over. Minimised and maximised are followed
// from what the guest reports its window became.
class MirrorWindow : public QWidget {
    Q_OBJECT
public:
    MirrorWindow(GuestLink& link, const GuestWindowInfo& window,
                 QWidget* parent = nullptr);

    [[nodiscard]] std::uint64_t handle() const noexcept { return handle_; }
    [[nodiscard]] bool is_owned() const noexcept { return owner_ != 0; }
    // Only a menu is placed from its owner. Anything else is a toplevel,
    // which on Wayland the client cannot position at all.
    [[nodiscard]] bool is_placed_by_owner() const noexcept {
        return owner_ != 0 && placed_by_owner_;
    }

    void present(const QImage& pixels);
    void retitle(const QString& title);
    void note_guest_size(std::uint32_t width, std::uint32_t height);
    void wear_guest_cursor(const QCursor& cursor);

    // Windows started moving this window and the guest stopped it. The
    // drag belongs to the compositor that actually has the window.
    void take_over_drag();

    // Follow the state the guest window ended up in. That is how a press
    // on chrome this side does not interpret still reaches this window.
    void follow_guest_state(bool minimized, bool maximized);

    // Put this window where the guest put it, relative to its owner.
    // ``guest_offset`` is in guest pixels.
    //
    // Wayland has no global coordinates, so a popup is positioned by its
    // offset from its parent surface and nothing else. Being a child of
    // the owner widget is what gives Qt that parent — without it the
    // compositor is handed a popup with nothing to be relative to, and
    // puts it in the middle of the window.
    void place_under(MirrorWindow& owner, QPoint guest_offset);

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
    [[nodiscard]] bool start_edge_resize(QMouseEvent* event);
    [[nodiscard]] QPoint frame_origin() const;
    [[nodiscard]] QPoint guest_position_of(QMouseEvent* event) const;
    void send_key(QKeyEvent* event, bool pressed);

    GuestLink& link_;
    std::uint64_t handle_;
    std::uint64_t owner_;
    QImage frame_;
    // One guest pixel is one logical unit here, deliberately.
    //
    // A scaled desktop makes those different sizes on the glass, and that
    // is the point: a native window of N units and a guest window of N
    // pixels take up the same share of their own screen, so the mirror
    // keeps the proportions the application was laid out for. Dividing by
    // the scale would shrink every mirror below what it looks like in the
    // guest. Positions and offsets follow the same rule, which is what
    // keeps a popup where the guest put it.
    QSize guest_size_;
    ResizeBorder border_;
    QCursor guest_cursor_;
    Qt::Edges hovered_edges_;
    bool placed_by_owner_ = false;
    bool guest_closed_ = false;
    bool applying_guest_size_ = false;
    bool applying_guest_state_ = false;
    // The press that began a drag or a resize was consumed by it; its
    // release would otherwise reach the guest as a click nobody made.
    bool swallow_release_ = false;
};

}  // namespace rwin::host
