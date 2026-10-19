#pragma once

#include "host/export/shared_frame.hpp"
#include "host/export/unique_fd.hpp"
#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
#include "host/geometry/view_mapping.hpp"
#include "host/wayland/picture_cutout.hpp"
#include "host/link/pixels_view.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "host/wayland/dmabuf_buffers.hpp"
#include "host/wayland/presentation_times.hpp"
#include "host/wayland/shm_buffers.hpp"
#include "host/wayland/surface_kit.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct wl_buffer;
struct wl_output;
struct wl_surface;
struct wp_alpha_modifier_surface_v1;
struct wp_fractional_scale_v1;
struct wp_linux_drm_syncobj_surface_v1;
struct wp_viewport;

namespace rwin::host {

class SurfaceInput;
class SyncTimeline;

// One wl_surface and the guest picture on it, as the compositor shows it.
//
// A frame of pixels is copied into shared memory of the surface's own,
// reused frame to frame. A frame that stayed on the GPU is shown as the
// guest's own buffer (zwp_linux_dmabuf_v1): with explicit sync
// (wp_linux_drm_syncobj_v1) the compositor waits for the guest's copy
// itself, and says when it is done with it; without, the copy is waited for
// here before the buffer is attached, and wl_buffer.release says when it is
// done. Either way the slot goes back to the guest only then.
//
// A guest pixel is a device pixel: the picture is shown at the surface's
// scale (wp_fractional_scale_v1, else the integer one the surface or its
// outputs have) through a viewport, the fraction of a logical unit a
// fractional scale leaves past its last whole unit cropped away
// (ViewMapping) - or stretched to a size it is given.
//
// Nothing is committed with a buffer until the surface's role allows it
// (allow_commits): an xdg surface's first commit has to be without one.
class MirrorSurface {
public:
    MirrorSurface(const SurfaceKit& kit, SurfaceInput& input);
    ~MirrorSurface();

    MirrorSurface(const MirrorSurface&) = delete;
    MirrorSurface& operator=(const MirrorSurface&) = delete;
    MirrorSurface(MirrorSurface&&) = delete;
    MirrorSurface& operator=(MirrorSurface&&) = delete;

    [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
    // What the seat's input on this surface is told to. The surface's own
    // listener data is this object, which is how the seat finds it
    // (input_of).
    [[nodiscard]] SurfaceInput& input() const noexcept { return input_; }
    [[nodiscard]] static SurfaceInput* input_of(wl_surface* surface) noexcept;

    // Device pixels per logical unit: the compositor's word for this
    // surface, or what it fell back to until it has one.
    [[nodiscard]] double scale() const noexcept { return scale_ > 0.0 ? scale_ : fallback_scale_; }
    void fall_back_to(double scale);
    // Whether the compositor has said this surface's scale yet.
    [[nodiscard]] bool scale_known() const noexcept { return scale_ > 0.0; }

    void show(const PixelsView& pixels);
    void show(const SharedFrame& frame);
    // The picture shown, or about to be; empty before the first.
    [[nodiscard]] Size picture_size() const noexcept { return picture_; }
    // A buffer has been committed.
    [[nodiscard]] bool mapped() const noexcept { return mapped_; }

    // One guest pixel to one device pixel (the default), or stretched over
    // ``logical`` units.
    void fit_one_to_one();
    void fit_to(Size logical);
    // How the picture lies on the surface as it is shown now.
    [[nodiscard]] ViewMapping mapping() const noexcept;

    // Where input lands: the picture rectangles given (empty: all of it),
    // or nowhere.
    void shape_input(std::vector<Box> picture_rects);
    void take_no_input(bool none);
    // A layered window's constant alpha, where the compositor can apply it.
    void set_opacity(std::uint8_t alpha);
    // What of the picture is not seen: texels of ``color_key`` (a COLORREF,
    // SetLayeredWindowAttributes' LWA_COLORKEY), and whatever lies outside
    // ``region`` (picture rectangles, SetWindowRgn; empty for no region).
    // Only a picture in pixels is cut: while either is set a frame on the
    // GPU is refused (shared_refused), and the window goes to pixels. True
    // when it changed, for the picture shown to be shown again.
    bool cut_out(std::optional<std::uint32_t> color_key, std::vector<Box> region);

    // The role was configured: buffers may be committed from now on.
    void allow_commits();
    // What changed other than the picture - its size, shape, opacity -
    // committed now.
    void commit();

    Signal<> rescaled;
    // A frame on the GPU cannot be shown here: why.
    Signal<const std::string&> shared_refused;
    Signal<const FrameStamps&> presented;
    // The first buffer was committed.
    Signal<> first_mapped;
    // A picture of another size is about to be committed: what goes with it
    // - a window geometry, a place - is to be said now.
    Signal<> picture_resized;

private:
    struct Held {
        wl_buffer* buffer = nullptr;
        std::shared_ptr<const SlotLease> lease;
        // With explicit sync: readable once the compositor is done.
        UniqueFd released;
        std::unique_ptr<EventLoop::Watch> watch;
    };
    struct SyncObjects;

    static void preferred_scale(void* self, wp_fractional_scale_v1* fractional, std::uint32_t scale);
    static void entered_output(void* self, wl_surface* surface, wl_output* output);
    static void left_output(void* self, wl_surface* surface, wl_output* output);
    static void preferred_buffer_scale(void* self, wl_surface* surface, std::int32_t factor);
    static void preferred_buffer_transform(void* self, wl_surface* surface, std::uint32_t transform);

    void take_scale(double scale);
    void attach_pixels();
    void try_attach_shared();
    void attach_shared(wl_buffer* buffer);
    void buffer_made(const SurfaceMemory& memory, wl_buffer* buffer, const std::string& why);
    void buffer_released(wl_buffer* buffer);
    void apply_view();
    void apply_input();
    void commit_attached(wl_buffer* buffer, const FrameStamps& stamps);
    void announce_resize();
    [[nodiscard]] bool explicitly_synced();
    void drop_sync_objects();

    SurfaceKit kit_;
    SurfaceInput& input_;
    wl_surface* surface_;
    wp_viewport* viewport_;
    wp_fractional_scale_v1* fractional_ = nullptr;
    wp_alpha_modifier_surface_v1* alpha_ = nullptr;
    std::unique_ptr<SyncObjects> sync_;
    ShmBuffers shm_;
    DmabufBuffers dmabufs_;
    PresentationTimes presentation_;
    double scale_ = 0.0;
    double fallback_scale_ = 1.0;
    Size picture_;
    std::optional<Size> fitted_;
    std::vector<Box> input_rects_;
    bool no_input_ = false;
    PictureCutout cutout_;
    bool commits_allowed_ = false;
    // A new picture's size is being said: nothing commits before its buffer.
    bool attaching_ = false;
    bool mapped_ = false;
    // A pixel frame copied and not yet committed.
    ShmBuffer* pixels_waiting_ = nullptr;
    FrameStamps pixels_stamps_;
    // The newest frame on the GPU not yet committed: its buffer is being
    // made, its copy waited for, or the role is not configured yet.
    std::optional<SharedFrame> shared_waiting_;
    std::unique_ptr<EventLoop::Watch> fence_watch_;
    std::vector<Held> held_;
};

}  // namespace rwin::host
