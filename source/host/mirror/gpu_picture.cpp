#include "host/mirror/gpu_picture.hpp"

#include "logging/logger.hpp"

#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSocketNotifier>
#include <QWindow>

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

// A guest's copy that has not landed in this long is not landing; drawn
// anyway rather than holding the window's paint up.
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

GpuPicture::GpuPicture(QWidget* parent) : QOpenGLWidget(parent), egl_(std::make_unique<EglCalls>()) {
    sample_pixels_ = qEnvironmentVariableIsSet("RWIN_TIMING_PIXELS");
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFocusPolicy(Qt::NoFocus);
    // Drawn whole each time: a picture is never partly updated.
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    connect(this, &QOpenGLWidget::frameSwapped, this, [this] {
        timing_.note("swap", drawn_order_);
    });
}

// Let go of here, then cut off from the context: QOpenGLWidget's destructor
// destroys it after this one has run, and its aboutToBeDestroyed would call
// release_gl on what is by then no GpuPicture - which aborted the mirror
// whenever a window closed, and with it every window the application went on
// to open.
GpuPicture::~GpuPicture() {
    release_gl();
    if (QOpenGLContext* gl = context(); gl != nullptr) disconnect(gl, nullptr, this, nullptr);
}

QSize GpuPicture::picture_size() const { return current_.has_value() ? current_->size() : QSize(); }

void GpuPicture::lay_out(const PictureLayout& layout) {
    if (layout == layout_) return;
    layout_ = layout;
    update();
}

// A frame replaced before it was drawn was never read here: its slot goes
// back at once. One that was drawn waits for the GPU to finish with it.
void GpuPicture::show_frame(SharedFrame frame) {
    const QWindow* surface = window()->windowHandle();
    const std::uint64_t visibility = (isVisible() ? 1u : 0u)
        | (surface != nullptr && surface->isExposed() ? 2u : 0u)
        | (window()->isMinimized() ? 4u : 0u);
    timing_.note("receive", frame.order, 0, visibility);
    if (current_.has_value() && current_drawn_) retiring_.push_back(std::move(*current_));
    current_ = std::move(frame);
    current_drawn_ = false;
    update();
}

// Nothing will be drawn here to fence the frames read so far behind, so the
// GPU is waited for now: this is the end of the GPU path for this mirror,
// not something that happens per frame.
void GpuPicture::forget() {
    if (!current_.has_value() && retiring_.empty() && waiting_.empty()) return;
    if (context() != nullptr) {
        makeCurrent();
        context()->functions()->glFinish();
        doneCurrent();
    }
    current_.reset();
    current_drawn_ = false;
    retiring_.clear();
    for (Retiring& waiting : waiting_) delete waiting.notifier;
    waiting_.clear();
    update();
}

void GpuPicture::initializeGL() {
    QOpenGLContext* gl = context();
    connect(gl, &QOpenGLContext::aboutToBeDestroyed, this, &GpuPicture::release_gl, Qt::DirectConnection);
    const auto* native = gl->nativeInterface<QNativeInterface::QEGLContext>();
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
        return;
    }
    if (!build_program()) return;
    vertices_ = std::make_unique<QOpenGLVertexArrayObject>();
    static_cast<void>(vertices_->create());
}

bool GpuPicture::build_program() {
    const bool gles = context()->isOpenGLES();
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

QSize GpuPicture::device_size() const {
    // As Qt sizes the framebuffer: QSize * qreal, rounded.
    return size() * devicePixelRatio();
}

void GpuPicture::paintGL() {
    QOpenGLFunctions* gl = context()->functions();
    gl->glDisable(GL_BLEND);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    forget_imports_not_shown();
    if (!current_.has_value() || program_ == nullptr || failed_) {
        retire_after_draw();
        return;
    }
    if (layout_.bars) fill_bars(device_size());
    const std::optional<GLuint> texture = texture_for(current_->memory);
    if (!texture.has_value()) {
        retire_after_draw();
        return;
    }
    wait_for_copy(*current_);
    draw(*texture);
    drawn_order_ = current_->order;
    timing_.note("draw", drawn_order_);
    if (sample_pixels_) {
        unsigned char pixel[4]{};
        gl->glReadPixels(device_size().width() / 2, device_size().height() / 2,
                         1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        timing_.note("pixel", drawn_order_, 0, pixel[0]);
    }
    current_drawn_ = true;
    retire_after_draw();
}

// The four bands round the picture, each clamped: a picture larger than its
// mirror, while the guest catches up with a resize, has no band there.
void GpuPicture::fill_bars(QSize device) {
    QOpenGLFunctions* gl = context()->functions();
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

void GpuPicture::draw(GLuint texture) {
    QOpenGLFunctions* gl = context()->functions();
    const QSize device = device_size();
    const QRect& picture = layout_.picture;
    const QSize source = current_->size();
    program_->bind();
    if (vertices_->isCreated()) vertices_->bind();
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    program_->setUniformValue("picture", 0);
    gl->glUniform2i(program_->uniformLocation("picture_size"), source.width(), source.height());
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
    QOpenGLFunctions* gl = context()->functions();
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
// window it stopped sharing: nothing will be drawn from it again.
void GpuPicture::forget_imports_not_shown() {
    const auto unused = std::remove_if(imports_.begin(), imports_.end(), [&](Import& import) {
        if (import.memory.use_count() > 1) return false;
        destroy(import);
        return true;
    });
    imports_.erase(unused, imports_.end());
}

void GpuPicture::destroy(Import& import) {
    if (import.texture != 0) context()->functions()->glDeleteTextures(1, &import.texture);
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

// One fence after this draw covers every draw before it on this context:
// the GPU runs them in order. Flushed, so the fence is there to wait on.
void GpuPicture::retire_after_draw() {
    if (retiring_.empty()) return;
    QOpenGLFunctions* gl = context()->functions();
    EGLSyncKHR sync = egl_->create_sync(egl_->display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    gl->glFlush();
    UniqueFd fence{sync != EGL_NO_SYNC_KHR ? egl_->dup_fence(egl_->display, sync) : -1};
    if (sync != EGL_NO_SYNC_KHR) egl_->destroy_sync(egl_->display, sync);
    if (!fence.valid()) {
        gl->glFinish();
        retiring_.clear();
        return;
    }
    auto* notifier = new QSocketNotifier(fence.get(), QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, notifier] { retired(notifier); });
    waiting_.push_back(Retiring{notifier, std::move(fence), std::move(retiring_)});
    retiring_.clear();
}

void GpuPicture::retired(QSocketNotifier* notifier) {
    const auto done = std::find_if(waiting_.begin(), waiting_.end(),
                                   [&](const Retiring& waiting) { return waiting.notifier == notifier; });
    if (done == waiting_.end()) return;
    notifier->setEnabled(false);
    notifier->deleteLater();
    waiting_.erase(done);
}

void GpuPicture::release_gl() {
    if (context() == nullptr || (imports_.empty() && program_ == nullptr && vertices_ == nullptr)) return;
    makeCurrent();
    for (Import& import : imports_) destroy(import);
    imports_.clear();
    program_.reset();
    vertices_.reset();
    doneCurrent();
}

// Said once, and after the paint that found it: whoever hears it takes the
// GPU path away, which is not a thing to do from inside a paint.
void GpuPicture::fail(const QString& why) {
    if (failed_) return;
    failed_ = true;
    RWIN_WARN("export", "a mirror cannot draw from the GPU: {}", why.toStdString());
    QMetaObject::invokeMethod(this, [this, why] { Q_EMIT failed(why); }, Qt::QueuedConnection);
}

}  // namespace rwin::host
