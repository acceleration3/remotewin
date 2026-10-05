#include "host/export/rendering_gpu.hpp"

#include "host/export/export_gpu.hpp"
#include "logging/logger.hpp"

#include <QOpenGLContext>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstring>

namespace rwin::host {

namespace {

// Importing the dma-buf, and the two fences: waiting on the guest's copy,
// and knowing when a draw that read a slot is done.
constexpr const char* kNeededExtensions[] = {
    "EGL_EXT_image_dma_buf_import",
    "EGL_ANDROID_native_fence_sync",
    "EGL_KHR_wait_sync",
};

bool has_extension(const char* extensions, const char* name) {
    const std::size_t length = std::strlen(name);
    for (const char* at = std::strstr(extensions, name); at != nullptr; at = std::strstr(at + 1, name)) {
        const char after = at[length];
        if ((at == extensions || at[-1] == ' ') && (after == ' ' || after == '\0')) return true;
    }
    return false;
}

// The DRM node the display is on: its render node where EGL names one.
std::optional<std::string> drm_node_of(EGLDisplay display) {
    const auto query_display =
        reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(eglGetProcAddress("eglQueryDisplayAttribEXT"));
    const auto query_device =
        reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(eglGetProcAddress("eglQueryDeviceStringEXT"));
    if (query_display == nullptr || query_device == nullptr) return std::nullopt;
    EGLAttrib device = 0;
    if (query_display(display, EGL_DEVICE_EXT, &device) != EGL_TRUE || device == 0) return std::nullopt;
    const auto named = reinterpret_cast<EGLDeviceEXT>(device);
    const char* node = query_device(named, EGL_DRM_RENDER_NODE_FILE_EXT);
    if (node == nullptr) node = query_device(named, EGL_DRM_DEVICE_FILE_EXT);
    if (node == nullptr) return std::nullopt;
    return std::string(node);
}

}  // namespace

RenderingGpu::RenderingGpu() {
    QOpenGLContext context;
    if (!context.create()) {
        lacking_ = "no OpenGL context";
        return;
    }
    const auto* native = context.nativeInterface<QNativeInterface::QEGLContext>();
    if (native == nullptr) {
        lacking_ = "Qt does not draw through EGL here";
        return;
    }
    const EGLDisplay display = native->display();
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    for (const char* needed : kNeededExtensions) {
        if (extensions == nullptr || !has_extension(extensions, needed)) {
            lacking_ = std::string("EGL here has no ") + needed;
            return;
        }
    }
    const std::optional<std::string> node = drm_node_of(display);
    if (!node.has_value()) {
        lacking_ = "EGL does not say which DRM device it draws on";
        return;
    }
    pci_ = PciAddress::of_drm_node(*node);
    if (!pci_.has_value()) {
        lacking_ = "the DRM device EGL draws on (" + *node + ") is on no PCI bus";
        return;
    }
    RWIN_INFO("export", "windows are drawn on {} ({})", pci_->to_string(), *node);
}

std::optional<std::string> RenderingGpu::refusal_for(const ExportGpu& exported) const {
    if (!lacking_.empty()) return lacking_;
    if (!exported.pci.has_value()) return std::string("the GPU device's GPU is on no PCI bus");
    if (*exported.pci != *pci_) {
        return "the guest's surfaces are on " + exported.pci->to_string() + ", windows here are drawn on "
               + pci_->to_string();
    }
    return std::nullopt;
}

}  // namespace rwin::host
