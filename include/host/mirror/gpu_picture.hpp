#pragma once

#include "host/export/shared_frame.hpp"
#include "host/latency/frame_stamps.hpp"
#include "host/mirror/picture_layout.hpp"
#include "logging/frame_timing.hpp"

#include <QImage>
#include <QSize>
#include <QWindow>

#include <qopengl.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QOpenGLContext;
class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class QSocketNotifier;

namespace rwin::host {

class PresentationTimes;

// A mirror's picture when the guest's frames stay on the GPU: each frame's
// memory imported as an EGL image (dma-buf, the layout its driver kept
// with it), copied once into a picture of this side's own, and drawn from
// that - exactly as the pixel path's QPainter draws a QImage - straight
// into a surface of its own, so nothing of a frame is ever copied by the
// CPU here.
//
// A window of its own rather than a part of the mirror's: a native child,
// which the compositor holds as a subsurface over the mirror. Drawn into a
// part of the mirror's own window, a frame was drawn twice - into that
// part, then with the rest of the window into the window's buffer - and
// waited for the compositor's next frame before it went. Here it is drawn
// once, swapped as soon as it is drawn, and the mirror's own pixels are not
// touched by it. It takes no input: the mirror under it keeps its events,
// its cursor and its shape. Shown in a QWidget::createWindowContainer.
//
// The guest copied into the frame's memory on its own GPU; that copy is
// waited for on the GPU before the frame is copied from (the fence the GPU
// device gave). The slot is let go of as soon as this side's copy of it is
// done on the GPU, so the guest has it back a frame or more sooner than if
// it waited for the draw that replaced it - and every draw, and every
// redraw for a resize or an expose, reads this side's copy.
class GpuPicture : public QWindow {
    Q_OBJECT
public:
    GpuPicture();
    ~GpuPicture() override;

    GpuPicture(const GpuPicture&) = delete;
    GpuPicture& operator=(const GpuPicture&) = delete;
    GpuPicture(GpuPicture&&) = delete;
    GpuPicture& operator=(GpuPicture&&) = delete;

    void show_frame(SharedFrame frame);
    // No frame from here on; every slot held is let go of now.
    void forget();
    [[nodiscard]] bool has_frame() const noexcept { return picture_size_.isValid(); }
    [[nodiscard]] QSize picture_size() const noexcept { return picture_size_; }

    void lay_out(const PictureLayout& layout);

    // What a draw puts in this window, drawn instead into an image of the
    // window's device size: for a harness that compares it with the guest.
    [[nodiscard]] QImage drawn();

Q_SIGNALS:
    // A frame could not be drawn from the GPU at all: the guest's frames
    // have to come as pixels.
    void failed(const QString& why);
    // A frame drawn here reached the screen, and these are its stamps: up
    // to Presented where the compositor says when, up to Swapped where not.
    void frame_timed(const rwin::host::FrameStamps& stamps);

protected:
    void exposeEvent(QExposeEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct EglCalls;
    // A slot's memory, imported once: an EGL image and the texture over it.
    struct Import {
        std::shared_ptr<const SurfaceMemory> memory;
        void* image = nullptr;
        GLuint texture = 0;
    };
    // A frame copied from, held until the copy is known done on the GPU.
    struct Copying {
        QSocketNotifier* notifier = nullptr;
        UniqueFd fence;
        SharedFrame frame;
    };
    // Frames drawn and not yet known to be on screen, by the number they
    // were asked for under; the oldest given up on past kMostAwaited.
    struct Awaited {
        std::uint64_t frame = 0;
        FrameStamps stamps;
    };
    static constexpr std::size_t kMostAwaited = 8;

    [[nodiscard]] bool make_current();
    [[nodiscard]] bool initialize_gl();
    [[nodiscard]] bool build_program();
    [[nodiscard]] std::optional<GLuint> texture_for(const std::shared_ptr<const SurfaceMemory>& memory);
    void forget_imports_not_shown();
    void destroy(Import& import);
    [[nodiscard]] bool copy_in(SharedFrame& frame);
    [[nodiscard]] bool size_picture(QSize size);
    void wait_for_copy(SharedFrame& frame);
    void let_go_after_copy(SharedFrame frame);
    void copied(QSocketNotifier* notifier);
    void render();
    void draw(QSize device, GLuint framebuffer);
    void fill_bars(QSize device);
    void await_screen();
    void presented(quint64 frame, qint64 when, qint64 refresh);
    void discarded(quint64 frame);
    void release_gl();
    void fail(const QString& why);
    [[nodiscard]] QSize device_size() const;

    std::unique_ptr<QOpenGLContext> context_;
    std::unique_ptr<EglCalls> egl_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLVertexArrayObject> vertices_;
    std::vector<Import> imports_;
    // This side's copy of the newest frame, which every draw reads.
    GLuint picture_ = 0;
    GLuint picture_framebuffer_ = 0;
    GLuint source_framebuffer_ = 0;
    QSize picture_size_;
    // A frame that came while there was nothing to draw it with yet - the
    // window not made, or not on screen - copied in once there is.
    std::optional<SharedFrame> waiting_;
    std::vector<Copying> copying_;
    // The newest frame's stamps until its first draw has gone.
    std::optional<FrameStamps> undrawn_;
    std::vector<Awaited> awaited_;
    std::uint64_t asked_ = 0;
    PictureLayout layout_;
    bool failed_ = false;
    FrameTiming timing_{"gpu"};
    std::uint64_t drawn_order_ = 0;
    bool sample_pixels_ = false;
    PresentationTimes* presentation_;
};

}  // namespace rwin::host
