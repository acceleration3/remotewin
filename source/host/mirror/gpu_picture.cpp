#include "host/mirror/gpu_picture.hpp"

#include "host/wayland/pointer_globals.hpp"
#include "host/wayland/presentation_times.hpp"
#include "logging/logger.hpp"

#include <QExposeEvent>
#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSocketNotifier>
#include <QSurfaceFormat>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <algorithm>
#include <cmath>

#include <poll.h>
#include <unistd.h>

namespace rwin::host {

// The EGL calls a picture drawn from shared memory needs, all of them
// extensions RenderingGpu checked the display has.
struct GpuPicture::EglCalls {
    EGLDisplay display = EGL_NO_DISPLAY;
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    PFNEGLCREATESYNCKHRPROC create_sync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync = nullptr;
    PFNEGLWAITSYNCKHRPROC wait_sync = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fence = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target = nullptr;

    [[nodiscard]] bool complete() const noexcept {
        return display != EGL_NO_DISPLAY && create_image != nullptr && destroy_image != nullptr
               && create_sync != nullptr && destroy_sync != nullptr && wait_sync != nullptr && dup_fence != nullptr
               && image_target != nullptr;
    }
};

namespace {

// A guest's copy that has not landed in this long is not landing; copied
// from anyway rather than holding the mirror up.
constexpr int kCopyWaitMs = 1000;

// One triangle over the whole viewport; the fragment shader finds its texel
// from where on the framebuffer it is, so the picture's place is exact to
// the device pixel whatever the rounding of a quad would be.
constexpr const char* kVertexShader = R"(
void main() {
    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// texelFetch rather than a sampler's filtering: a colour key is matched
// against each texel the guest drew, before any resampling mixes it with a
// neighbour, as the pixel path keys the frame before QPainter scales it.
// Fitted, the four neighbours are blended as a bilinear sampler would.
constexpr const char* kFragmentShader = R"(
uniform sampler2D picture;
uniform ivec2 picture_size;
// x and y of the picture's top left on the framebuffer (y from the bottom,
// as gl_FragCoord is), and the device pixels it covers.
uniform vec4 placement;
uniform int smoothed;
uniform float opacity;
uniform int keyed;
uniform vec3 key;
out vec4 colour;

vec4 texel(ivec2 at) {
    vec4 drawn = texelFetch(picture, clamp(at, ivec2(0), picture_size - 1), 0);
    if (keyed != 0 && all(equal(floor(drawn.rgb * 255.0 + 0.5), key))) return vec4(0.0);
    return drawn;
}

void main() {
    vec2 from_corner = vec2(gl_FragCoord.x - placement.x, placement.y - gl_FragCoord.y);
    if (smoothed == 0) {
        colour = texel(ivec2(floor(from_corner))) * opacity;
        return;
    }
    vec2 at = from_corner * vec2(picture_size) / placement.zw - 0.5;
    vec2 base = floor(at);
    vec2 weight = at - base;
    ivec2 corner = ivec2(base);
    vec4 top = mix(texel(corner), texel(corner + ivec2(1, 0)), weight.x);
    vec4 bottom = mix(texel(corner + ivec2(0, 1)), texel(corner + ivec2(1, 1)), weight.x);
    colour = mix(top, bottom, weight.y) * opacity;
}
)";

QByteArray versioned(const char* source, bool gles, bool fragment) {
    QByteArray text = gles ? QByteArrayLiteral("#version 300 es\n") : QByteArrayLiteral("#version 130\n");
    if (gles && fragment) text += QByteArrayLiteral("precision highp float;\nprecision highp int;\n");
    return text + source;
}

}  // namespace

GpuPicture::GpuPicture() : egl_(std::make_unique<EglCalls>()), presentation_(new PresentationTimes(this)) {
    sample_pixels_ = qEnvironmentVariableIsSet("RWIN_TIMING_PIXELS");
    setSurfaceType(QSurface::OpenGLSurface);
    setFlag(Qt::WindowTransparentForInput);
    QSurfaceFormat wanted = QSurfaceFormat::defaultFormat();
    wanted.setAlphaBufferSize(8);
    wanted.setDepthBufferSize(0);
    wanted.setStencilBufferSize(0);
    // Swapped the moment it is drawn: the compositor takes whatever came
    // last when it next draws, and a swap that waited for it would hold
    // every frame back to that.
    wanted.setSwapInterval(0);
    setFormat(wanted);
    connect(presentation_, &PresentationTimes::presented, this, &GpuPicture::presented);
    connect(presentation_, &PresentationTimes::discarded, this, &GpuPicture::discarded);
}

// Let go of here, then cut off from the context, whose destruction would
// otherwise call release_gl on what is by then no GpuPicture.
GpuPicture::~GpuPicture() {
    release_gl();
    if (context_ != nullptr) disconnect(context_.get(), nullptr, this, nullptr);
    copying_.clear();
}

QSize GpuPicture::device_size() const {
    // As Qt sizes the window's buffer: QSize * qreal, rounded.
    return size() * devicePixelRatio();
}

bool GpuPicture::make_current() {
    if (handle() == nullptr || failed_) return false;
    if (context_ == nullptr && !initialize_gl()) return false;
    return context_->makeCurrent(this);
}

bool GpuPicture::initialize_gl() {
    context_ = std::make_unique<QOpenGLContext>();
    context_->setFormat(requestedFormat());
    if (!context_->create() || !context_->makeCurrent(this)) {
        fail(QStringLiteral("no OpenGL context for the picture"));
        return false;
    }
    connect(context_.get(), &QOpenGLContext::aboutToBeDestroyed, this, &GpuPicture::release_gl,
            Qt::DirectConnection);
    const auto* native = context_->nativeInterface<QNativeInterface::QEGLContext>();
    egl_->display = native != nullptr ? native->display() : EGL_NO_DISPLAY;
    egl_->create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    egl_->destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    egl_->create_sync = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
    egl_->destroy_sync = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
    egl_->wait_sync = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(eglGetProcAddress("eglWaitSyncKHR"));
    egl_->dup_fence =
        reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(eglGetProcAddress("eglDupNativeFenceFDANDROID"));
    egl_->image_target =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!egl_->complete()) {
        fail(QStringLiteral("this OpenGL context cannot import a dma-buf or wait on a fence"));
        return false;
    }
    if (!build_program()) return false;
    vertices_ = std::make_unique<QOpenGLVertexArrayObject>();
    static_cast<void>(vertices_->create());
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    gl->glGenFramebuffers(1, &picture_framebuffer_);
    gl->glGenFramebuffers(1, &source_framebuffer_);
    return true;
}

bool GpuPicture::build_program() {
    const bool gles = context_->isOpenGLES();
    program_ = std::make_unique<QOpenGLShaderProgram>();
    if (!program_->addShaderFromSourceCode(QOpenGLShader::Vertex, versioned(kVertexShader, gles, false))
        || !program_->addShaderFromSourceCode(QOpenGLShader::Fragment, versioned(kFragmentShader, gles, true))
        || !program_->link()) {
        fail(QStringLiteral("the picture's shaders do not build here: ") + program_->log());
        program_.reset();
        return false;
    }
    return true;
}

// The newest frame wins: one that comes while another still waits for the
// window to be on screen replaces it, and the replaced one's slot goes back.
void GpuPicture::show_frame(SharedFrame frame) {
    const QWindow* top = this;
    while (top->parent() != nullptr) top = top->parent();
    const std::uint64_t visibility = (isVisible() ? 1u : 0u) | (isExposed() ? 2u : 0u)
                                     | (top->windowStates().testFlag(Qt::WindowMinimized) ? 4u : 0u);
    timing_.note("receive", frame.order, 0, visibility);
    frame.stamps.note_now(FrameStamps::Stage::Shown);
    if (failed_) return;
    if (!isExposed() || !make_current()) {
        waiting_ = std::move(frame);
        return;
    }
    waiting_.reset();
    if (copy_in(frame)) render();
}

// Nothing will be drawn from here on, so the GPU is waited for now: this is
// the end of the GPU path for this mirror, not something that happens per
// frame.
void GpuPicture::forget() {
    waiting_.reset();
    undrawn_.reset();
    if (make_current()) {
        context_->functions()->glFinish();
        if (picture_ != 0) context_->functions()->glDeleteTextures(1, &picture_);
        picture_ = 0;
    }
    for (Copying& copying : copying_) delete copying.notifier;
    copying_.clear();
    picture_size_ = QSize();
    render();
}

void GpuPicture::lay_out(const PictureLayout& layout) {
    if (layout == layout_) return;
    layout_ = layout;
    render();
}

void GpuPicture::exposeEvent(QExposeEvent* /*event*/) {
    if (!isExposed()) return;
    if (waiting_.has_value() && make_current()) {
        SharedFrame frame = std::move(*waiting_);
        waiting_.reset();
        static_cast<void>(copy_in(frame));
    }
    render();
}

void GpuPicture::resizeEvent(QResizeEvent* /*event*/) { render(); }

// The guest's frame into this side's picture, on the GPU, after the guest's
// copy into it: from here on nothing reads the slot.
bool GpuPicture::copy_in(SharedFrame& frame) {
    forget_imports_not_shown();
    const std::optional<GLuint> texture = texture_for(frame.memory);
    if (!texture.has_value() || !size_picture(frame.size())) return false;
    wait_for_copy(frame);
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, source_framebuffer_);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *texture, 0);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, picture_framebuffer_);
    const QSize size = frame.size();
    gl->glBlitFramebuffer(0, 0, size.width(), size.height(), 0, 0, size.width(), size.height(), GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    gl->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    drawn_order_ = frame.order;
    undrawn_ = frame.stamps;
    let_go_after_copy(std::move(frame));
    return true;
}

bool GpuPicture::size_picture(QSize size) {
    if (picture_ != 0 && size == picture_size_) return true;
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    if (picture_ == 0) gl->glGenTextures(1, &picture_);
    gl->glBindTexture(GL_TEXTURE_2D, picture_);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, picture_framebuffer_);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, picture_, 0);
    const GLenum complete = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (complete != GL_FRAMEBUFFER_COMPLETE) {
        fail(QStringLiteral("a %1x%2 picture cannot be drawn into here: 0x%3")
                 .arg(size.width())
                 .arg(size.height())
                 .arg(complete, 0, 16));
        return false;
    }
    picture_size_ = size;
    return true;
}

std::optional<GLuint> GpuPicture::texture_for(const std::shared_ptr<const SurfaceMemory>& memory) {
    const auto known = std::find_if(imports_.begin(), imports_.end(),
                                    [&](const Import& import) { return import.memory == memory; });
    if (known != imports_.end()) return known->texture;
    const ExportedLayout& layout = memory->layout();
    // No modifier attribute for an implicit modifier: the driver reads the
    // layout it kept with the memory, which is what implicit is.
    std::vector<EGLint> attributes{EGL_WIDTH,
                                   static_cast<EGLint>(layout.width),
                                   EGL_HEIGHT,
                                   static_cast<EGLint>(layout.height),
                                   EGL_LINUX_DRM_FOURCC_EXT,
                                   static_cast<EGLint>(layout.fourcc),
                                   EGL_DMA_BUF_PLANE0_FD_EXT,
                                   memory->dma_buf(),
                                   EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                   static_cast<EGLint>(layout.offset),
                                   EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                   static_cast<EGLint>(layout.stride)};
    if (layout.modifier != ExportedLayout::kImplicitModifier) {
        attributes.insert(attributes.end(),
                          {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, static_cast<EGLint>(layout.modifier & 0xFFFFFFFF),
                           EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, static_cast<EGLint>(layout.modifier >> 32)});
    }
    attributes.push_back(EGL_NONE);
    EGLImageKHR image =
        egl_->create_image(egl_->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
    if (image == EGL_NO_IMAGE_KHR) {
        fail(QStringLiteral("EGL would not import surface %1 (%2x%3, pitch %4): error 0x%5")
                 .arg(memory->surface())
                 .arg(layout.width)
                 .arg(layout.height)
                 .arg(layout.stride)
                 .arg(eglGetError(), 0, 16));
        return std::nullopt;
    }
    QOpenGLFunctions* gl = context_->functions();
    Import import{memory, image, 0};
    gl->glGenTextures(1, &import.texture);
    gl->glBindTexture(GL_TEXTURE_2D, import.texture);
    egl_->image_target(GL_TEXTURE_2D, image);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (const GLenum error = gl->glGetError(); error != GL_NO_ERROR) {
        destroy(import);
        fail(QStringLiteral("the imported surface %1 is no texture here: GL error 0x%2")
                 .arg(memory->surface())
                 .arg(error, 0, 16));
        return std::nullopt;
    }
    RWIN_DEBUG("export", "imported surface {:#x}: {}x{}, pitch {}", memory->surface(), layout.width, layout.height,
               layout.stride);
    imports_.push_back(import);
    return import.texture;
}

// An import nobody but this holds is of a ring the guest replaced or a
// window it stopped sharing: nothing will be copied from it again.
void GpuPicture::forget_imports_not_shown() {
    const auto unused = std::remove_if(imports_.begin(), imports_.end(), [&](Import& import) {
        if (import.memory.use_count() > 1) return false;
        destroy(import);
        return true;
    });
    imports_.erase(unused, imports_.end());
}

void GpuPicture::destroy(Import& import) {
    if (import.texture != 0) context_->functions()->glDeleteTextures(1, &import.texture);
    if (import.image != nullptr) egl_->destroy_image(egl_->display, import.image);
    import.texture = 0;
    import.image = nullptr;
}

// On this context's GPU queue, where EGL can; on the CPU when it cannot
// take the fence. The fence is EGL's once the sync is made.
void GpuPicture::wait_for_copy(SharedFrame& frame) {
    if (frame.fence == nullptr || !frame.fence->valid()) return;
    const int fd = frame.fence->release();
    const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
    EGLSyncKHR sync = egl_->create_sync(egl_->display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
    if (sync == EGL_NO_SYNC_KHR) {
        const UniqueFd held{fd};
        pollfd watched{held.get(), POLLIN, 0};
        if (::poll(&watched, 1, kCopyWaitMs) != 1) RWIN_WARN("export", "a guest's copy was not done in time");
        return;
    }
    egl_->wait_sync(egl_->display, sync, 0);
    egl_->destroy_sync(egl_->display, sync);
}

// The frame's slot goes back once the GPU has run the copy out of it, and
// not before: the guest copies its next frame in as soon as it has it.
// Flushed, so the fence is there to wait on.
void GpuPicture::let_go_after_copy(SharedFrame frame) {
    QOpenGLFunctions* gl = context_->functions();
    EGLSyncKHR sync = egl_->create_sync(egl_->display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    gl->glFlush();
    UniqueFd fence{sync != EGL_NO_SYNC_KHR ? egl_->dup_fence(egl_->display, sync) : -1};
    if (sync != EGL_NO_SYNC_KHR) egl_->destroy_sync(egl_->display, sync);
    if (!fence.valid()) {
        gl->glFinish();
        return;
    }
    auto* notifier = new QSocketNotifier(fence.get(), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier] { copied(notifier); });
    copying_.push_back(Copying{notifier, std::move(fence), std::move(frame)});
}

void GpuPicture::copied(QSocketNotifier* notifier) {
    const auto done = std::find_if(copying_.begin(), copying_.end(),
                                   [&](const Copying& copying) { return copying.notifier == notifier; });
    if (done == copying_.end()) return;
    notifier->setEnabled(false);
    notifier->deleteLater();
    copying_.erase(done);
}

// Into the window, and swapped at once. The first draw of a frame asks the
// compositor when it is shown; a redraw of the same frame does not count.
void GpuPicture::render() {
    if (!isExposed() || program_ == nullptr || !make_current()) return;
    const QSize device = device_size();
    draw(device, context_->defaultFramebufferObject());
    if (undrawn_.has_value()) {
        undrawn_->note_now(FrameStamps::Stage::Drawn);
        timing_.note("draw", drawn_order_);
        if (sample_pixels_) {
            unsigned char pixel[4]{};
            context_->functions()->glReadPixels(device.width() / 2, device.height() / 2, 1, 1, GL_RGBA,
                                                GL_UNSIGNED_BYTE, pixel);
            timing_.note("pixel", drawn_order_, 0, pixel[0]);
        }
        await_screen();
    }
    context_->swapBuffers(this);
    if (!undrawn_.has_value()) return;
    timing_.note("swap", drawn_order_);
    undrawn_->note_now(FrameStamps::Stage::Swapped);
    if (presentation_->offered()) {
        awaited_.back().stamps = *undrawn_;
    } else {
        Q_EMIT frame_timed(*undrawn_);
    }
    undrawn_.reset();
}

void GpuPicture::draw(QSize device, GLuint framebuffer) {
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glViewport(0, 0, device.width(), device.height());
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    if (!has_frame()) return;
    if (layout_.bars) fill_bars(device);
    const QRect& picture = layout_.picture;
    program_->bind();
    if (vertices_->isCreated()) vertices_->bind();
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, picture_);
    program_->setUniformValue("picture", 0);
    gl->glUniform2i(program_->uniformLocation("picture_size"), picture_size_.width(), picture_size_.height());
    program_->setUniformValue("placement", QVector4D(static_cast<float>(picture.x()),
                                                     static_cast<float>(device.height() - picture.y()),
                                                     static_cast<float>(picture.width()),
                                                     static_cast<float>(picture.height())));
    program_->setUniformValue("smoothed", layout_.smooth ? 1 : 0);
    program_->setUniformValue("opacity", static_cast<float>(layout_.opacity));
    program_->setUniformValue("keyed", layout_.color_key.has_value() ? 1 : 0);
    const QRgb key = layout_.color_key.value_or(0);
    program_->setUniformValue("key", QVector3D(static_cast<float>(qRed(key)), static_cast<float>(qGreen(key)),
                                               static_cast<float>(qBlue(key))));
    const QRegion where = layout_.shape.isEmpty() ? QRegion(picture) : layout_.shape.intersected(picture);
    gl->glEnable(GL_SCISSOR_TEST);
    for (const QRect& part : where) {
        gl->glScissor(part.x(), device.height() - part.y() - part.height(), part.width(), part.height());
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    if (vertices_->isCreated()) vertices_->release();
    program_->release();
}

// The four bands round the picture, each clamped: a picture larger than its
// mirror, while the guest catches up with a resize, has no band there.
void GpuPicture::fill_bars(QSize device) {
    QOpenGLFunctions* gl = context_->functions();
    const QRegion bars = QRegion(QRect(QPoint(0, 0), device)) - QRegion(layout_.picture);
    gl->glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
    gl->glEnable(GL_SCISSOR_TEST);
    for (const QRect& bar : bars) {
        gl->glScissor(bar.x(), device.height() - bar.y() - bar.height(), bar.width(), bar.height());
        gl->glClear(GL_COLOR_BUFFER_BIT);
    }
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
}

QImage GpuPicture::drawn() {
    if (!make_current()) return {};
    const QSize device = device_size();
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    GLuint target = 0;
    GLuint framebuffer = 0;
    gl->glGenTextures(1, &target);
    gl->glBindTexture(GL_TEXTURE_2D, target);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, device.width(), device.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glGenFramebuffers(1, &framebuffer);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    draw(device, framebuffer);
    QImage image(device, QImage::Format_RGBA8888_Premultiplied);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    gl->glReadPixels(0, 0, device.width(), device.height(), GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glDeleteFramebuffers(1, &framebuffer);
    gl->glDeleteTextures(1, &target);
    // GL's rows go up from the bottom.
    return image.flipped(Qt::Vertical);
}

// Asked of this window's own surface before the swap commits it: the
// presentation feedback is for the next commit.
void GpuPicture::await_screen() {
    if (!presentation_->offered()) return;
    if (awaited_.size() == kMostAwaited) awaited_.erase(awaited_.begin());
    awaited_.push_back(Awaited{++asked_, *undrawn_});
    presentation_->ask(PointerGlobals::surface_of(this), asked_);
}

void GpuPicture::presented(quint64 frame, qint64 when, qint64 refresh) {
    const auto found = std::find_if(awaited_.begin(), awaited_.end(),
                                    [&](const Awaited& awaited) { return awaited.frame == frame; });
    if (found == awaited_.end()) return;
    FrameStamps stamps = found->stamps;
    awaited_.erase(found);
    stamps.note(FrameStamps::Stage::Presented, when);
    stamps.refresh_ns = refresh;
    Q_EMIT frame_timed(stamps);
}

void GpuPicture::discarded(quint64 frame) {
    std::erase_if(awaited_, [&](const Awaited& awaited) { return awaited.frame == frame; });
}

void GpuPicture::release_gl() {
    if (context_ == nullptr || !context_->makeCurrent(this)) return;
    QOpenGLExtraFunctions* gl = context_->extraFunctions();
    gl->glFinish();
    for (Import& import : imports_) destroy(import);
    imports_.clear();
    if (picture_ != 0) gl->glDeleteTextures(1, &picture_);
    if (picture_framebuffer_ != 0) gl->glDeleteFramebuffers(1, &picture_framebuffer_);
    if (source_framebuffer_ != 0) gl->glDeleteFramebuffers(1, &source_framebuffer_);
    picture_ = 0;
    picture_framebuffer_ = 0;
    source_framebuffer_ = 0;
    program_.reset();
    vertices_.reset();
    context_->doneCurrent();
}

// Said once, and after the draw that found it: whoever hears it takes the
// GPU path away, which is not a thing to do from inside a draw.
void GpuPicture::fail(const QString& why) {
    if (failed_) return;
    failed_ = true;
    RWIN_WARN("export", "a mirror cannot draw from the GPU: {}", why.toStdString());
    QMetaObject::invokeMethod(this, [this, why] { Q_EMIT failed(why); }, Qt::QueuedConnection);
}

}  // namespace rwin::host
