#pragma once

#include "host/export/shared_frame.hpp"
#include "host/export/unique_fd.hpp"
#include "host/geometry/box.hpp"
#include "host/geometry/point.hpp"
#include "host/geometry/point_f.hpp"
#include "host/geometry/size.hpp"
#include "host/geometry/view_mapping.hpp"
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

class MirrorSurface {
public:
    MirrorSurface(const SurfaceKit& kit, SurfaceInput& input);
    ~MirrorSurface();

    MirrorSurface(const MirrorSurface&) = delete;
    MirrorSurface& operator=(const MirrorSurface&) = delete;
    MirrorSurface(MirrorSurface&&) = delete;
    MirrorSurface& operator=(MirrorSurface&&) = delete;

    [[nodiscard]] wl_surface* surface() const noexcept { return surface_; }
    [[nodiscard]] SurfaceInput& input() const noexcept { return input_; }
    [[nodiscard]] static SurfaceInput* input_of(wl_surface* surface) noexcept;

    [[nodiscard]] double scale() const noexcept { return scale_ > 0.0 ? scale_ : fallback_scale_; }
    void fall_back_to(double scale);
    [[nodiscard]] bool scale_known() const noexcept { return scale_ > 0.0; }

    void show(const PixelsView& pixels);
    void show(const SharedFrame& frame);
    [[nodiscard]] Size picture_size() const noexcept { return picture_; }
    [[nodiscard]] bool mapped() const noexcept { return mapped_; }
    [[nodiscard]] bool on_gpu() const noexcept { return on_gpu_; }

    void fit_one_to_one();
    void crop_to(Size logical);
    void fit_to(Size logical);
    [[nodiscard]] ViewMapping mapping() const noexcept;

    void shape_input(std::vector<Box> picture_rects);
    void take_no_input(bool none);
    void set_opacity(std::uint8_t alpha);
    void shape_opaque(std::vector<Box> picture_rects, Size of);

    void allow_commits();
    void commit();

    Signal<> rescaled;
    Signal<const std::string&> shared_refused;
    Signal<const FrameStamps&> presented;
    Signal<> first_mapped;
    Signal<> picture_resized;
    Signal<> frame_owed;

private:
    struct Held {
        wl_buffer* buffer = nullptr;
        std::shared_ptr<const SlotLease> lease;
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
    void apply_opaque();
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
    std::optional<Size> cropped_;
    std::vector<Box> input_rects_;
    bool no_input_ = false;
    std::vector<Box> opaque_rects_;
    Size opaque_for_;
    bool commits_allowed_ = false;
    bool attaching_ = false;
    bool mapped_ = false;
    bool on_gpu_ = false;
    ShmBuffer* pixels_waiting_ = nullptr;
    bool frame_dropped_ = false;
    std::unique_ptr<EventLoop::Timer> owed_;
    FrameStamps pixels_stamps_;
    std::optional<SharedFrame> shared_waiting_;
    std::unique_ptr<EventLoop::Watch> fence_watch_;
    std::vector<Held> held_;
};

}
