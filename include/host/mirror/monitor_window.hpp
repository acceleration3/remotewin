#pragma once

#include "host/cursor/worn_cursor.hpp"
#include "host/input/held_keys.hpp"
#include "host/input/key_translator.hpp"

#include <QImage>
#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <optional>

namespace rwin::host {

struct CursorLook;
class GpuPicture;
class GuestLink;
class PixelCursor;
class PixelMapping;
struct SharedFrame;

// A guest's whole monitor, shown as one window of this desktop.
//
// An ordinary window - decorated, resizable, its own title - and the
// guest's screen does not follow its size: the guest's screen is what it
// is. The picture is one guest pixel to one device pixel, centred, when
// the window has room for that, and fitted - scaled down, letterboxed in
// black - when it has not, so the whole screen is always in view.
//
// The pointer goes to the guest pixel under it, and keys to whatever the
// guest has in front: the window is the monitor, not any window on it. The
// guest's cursor is not in the picture (the capture leaves it out); it is
// worn as this window's own, so it moves with no delay at all and only its
// shape comes from the guest.
class MonitorWindow : public QWidget {
    Q_OBJECT
public:
    MonitorWindow(GuestLink& link, const QString& title, PixelCursor& pixel_cursor);

    void present(const QImage& pixels);
    // Frames that stay on the GPU are drawn here from now on, as well as
    // frames of pixels. Before the window is first shown: it makes the
    // window an OpenGL one (GpuPicture).
    void take_gpu_frames();
    void present_shared(const SharedFrame& frame);
    // A part of the picture changed; one of another size than the one
    // shown is a patch of a picture not had yet, and waits for it.
    void patch(QSize picture, QPoint at, const QImage& pixels);
    void wear_guest_cursor(const CursorLook* look, std::size_t step);
    // The guest half dialled in; until then there is nothing to wait for
    // but it.
    void note_guest_connected();

Q_SIGNALS:
    void closed();
    // A frame that stayed on the GPU could not be drawn here.
    void gpu_failed(const QString& why);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    // The guest's screen as last shown, whichever way it came; empty before
    // the first frame.
    [[nodiscard]] QSize picture_size() const;
    [[nodiscard]] bool shows_gpu_frame() const;
    // A first picture sizes the window; one of another size is said.
    void note_picture(QSize before);
    void lay_out_gpu();
    // One to one where it fits, fitted where it does not; centred either way.
    [[nodiscard]] PixelMapping mapping() const;
    // The guest pixel under a point of this window; nothing over the bars.
    [[nodiscard]] std::optional<QPoint> guest_point_of(QPointF position) const;
    void send_button(QMouseEvent* event, bool pressed);
    void send_key(QKeyEvent* event, bool pressed);
    // As large as the guest's screen at one to one, or as much of this
    // desktop's screen as there is.
    void size_to(QSize guest);

    GuestLink& link_;
    KeyTranslator keys_;
    HeldKeys held_keys_;
    WornCursor worn_;
    QImage frame_;
    // The picture when the guest's frames stay on the GPU; null otherwise.
    GpuPicture* gpu_ = nullptr;
    bool guest_connected_ = false;
};

}  // namespace rwin::host
