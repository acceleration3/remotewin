#pragma once

#include "host/export/shared_frame.hpp"
#include "host/mirror/picture_layout.hpp"
#include "logging/frame_timing.hpp"

#include <QOpenGLWidget>
#include <QSize>

#include <memory>
#include <optional>
#include <vector>

class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class QSocketNotifier;

namespace rwin::host {

// A mirror's picture when the guest's frames stay on the GPU: each frame's
// memory imported as an EGL image (dma-buf, the layout its driver kept
// with it) and drawn from there, exactly as the pixel path's QPainter
// draws a QImage - so nothing of a frame is ever copied by the CPU here.
//
// A child that covers its mirror and takes no input: the mirror under it
// keeps its events, its cursor and its shape, and draws nothing where this
// is. Being a QOpenGLWidget makes the mirror's window an OpenGL one, so it
// is made before the mirror is first shown, when the window is made: made
// after, the window would be made again, and a window made again is one
// the compositor places afresh.
//
// The guest copied into the frame's memory on its own GPU; that copy is
// waited for on the GPU before the first draw from it (the fence the GPU
// device gave). A frame's slot is let go of once a later frame has been
// drawn and the GPU has finished that draw - and with it every draw before
// it that read the slot - so the guest never copies into what is still
// being read.
class GpuPicture : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit GpuPicture(QWidget* parent);
    ~GpuPicture() override;

    GpuPicture(const GpuPicture&) = delete;
    GpuPicture& operator=(const GpuPicture&) = delete;
    GpuPicture(GpuPicture&&) = delete;
    GpuPicture& operator=(GpuPicture&&) = delete;

    void show_frame(SharedFrame frame);
    // No frame from here on; every slot held is let go of now.
    void forget();
    [[nodiscard]] bool has_frame() const noexcept { return current_.has_value(); }
    [[nodiscard]] QSize picture_size() const;

    void lay_out(const PictureLayout& layout);

Q_SIGNALS:
    // A frame could not be drawn from the GPU at all: the guest's frames
    // have to come as pixels.
    void failed(const QString& why);

protected:
    void initializeGL() override;
    void paintGL() override;

private:
    struct EglCalls;
    // A slot's memory, imported once: an EGL image and the texture over it.
    struct Import {
        std::shared_ptr<const SurfaceMemory> memory;
        void* image = nullptr;
        GLuint texture = 0;
    };
    // Frames read by draws not yet known to be done, and the fence that
    // says when they are.
    struct Retiring {
        QSocketNotifier* notifier = nullptr;
        UniqueFd fence;
        std::vector<SharedFrame> frames;
    };

    [[nodiscard]] bool build_program();
    [[nodiscard]] std::optional<GLuint> texture_for(const std::shared_ptr<const SurfaceMemory>& memory);
    void forget_imports_not_shown();
    void destroy(Import& import);
    void wait_for_copy(SharedFrame& frame);
    void draw(GLuint texture);
    void fill_bars(QSize device);
    void retire_after_draw();
    void retired(QSocketNotifier* notifier);
    void release_gl();
    void fail(const QString& why);
    [[nodiscard]] QSize device_size() const;

    std::unique_ptr<EglCalls> egl_;
    std::unique_ptr<QOpenGLShaderProgram> program_;
    std::unique_ptr<QOpenGLVertexArrayObject> vertices_;
    std::vector<Import> imports_;
    std::optional<SharedFrame> current_;
    bool current_drawn_ = false;
    std::vector<SharedFrame> retiring_;
    std::vector<Retiring> waiting_;
    PictureLayout layout_;
    bool failed_ = false;
    FrameTiming timing_{"gpu"};
    std::uint64_t drawn_order_ = 0;
    bool sample_pixels_ = false;
};

}  // namespace rwin::host
