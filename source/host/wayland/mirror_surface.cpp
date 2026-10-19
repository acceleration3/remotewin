#include "host/wayland/mirror_surface.hpp"

#include "host/export/slot_lease.hpp"
#include "host/export/surface_memory.hpp"
#include "host/wayland/drm_sync_device.hpp"
#include "host/wayland/globals.hpp"
#include "host/wayland/seat.hpp"
#include "host/wayland/surface_input.hpp"
#include "host/wayland/sync_timeline.hpp"
#include "logging/logger.hpp"

#include <alpha-modifier-v1-client-protocol.h>
#include <fractional-scale-v1-client-protocol.h>
#include <linux-drm-syncobj-v1-client-protocol.h>
#include <viewporter-client-protocol.h>
#include <wayland-client.h>

#include <poll.h>

#include <algorithm>
#include <cstring>
#include <limits>

namespace rwin::host {

namespace {

constexpr double kFractionalDenominator = 120.0;
constexpr std::uint32_t kPreferredScaleVersion = 6;

bool signalled(int fence) {
    pollfd ready{fence, POLLIN, 0};
    return ::poll(&ready, 1, 0) > 0 && (ready.revents & POLLIN) != 0;
}

}  // namespace

// The surface's explicit sync: the syncobj surface and its two timelines,
// the guest's copies acquired on one and the compositor's releases on the
// other, at the same point for one commit.
struct MirrorSurface::SyncObjects {
    SyncObjects(const DrmSyncDevice& device, wl_surface* surface)
        : acquire(device), release(device),
          synced(wp_linux_drm_syncobj_manager_v1_get_surface(device.manager(), surface)) {}
    ~SyncObjects() { wp_linux_drm_syncobj_surface_v1_destroy(synced); }

    SyncObjects(const SyncObjects&) = delete;
    SyncObjects& operator=(const SyncObjects&) = delete;
    SyncObjects(SyncObjects&&) = delete;
    SyncObjects& operator=(SyncObjects&&) = delete;

    SyncTimeline acquire;
    SyncTimeline release;
    wp_linux_drm_syncobj_surface_v1* synced;
    std::uint64_t point = 0;
};

MirrorSurface::MirrorSurface(const SurfaceKit& kit, SurfaceInput& input)
    : kit_(kit), input_(input), surface_(wl_compositor_create_surface(kit.globals.compositor())),
      viewport_(wp_viewporter_get_viewport(kit.globals.viewporter(), surface_)), shm_(kit.globals.shm()),
      dmabufs_(
          kit.globals.dmabuf(),
          [this](const SurfaceMemory& memory, wl_buffer* buffer, const std::string& why) {
              buffer_made(memory, buffer, why);
          },
          [this](wl_buffer* buffer) { buffer_released(buffer); }),
      presentation_(kit.globals.presentation(), [this](const FrameStamps& stamps) { presented(stamps); }),
      fallback_scale_(kit.globals.output_scale()) {
    static constexpr wl_surface_listener kSurface{&MirrorSurface::entered_output, &MirrorSurface::left_output,
                                                  &MirrorSurface::preferred_buffer_scale,
                                                  &MirrorSurface::preferred_buffer_transform};
    wl_surface_add_listener(surface_, &kSurface, this);
    if (kit.globals.fractional_scale() != nullptr) {
        fractional_ = wp_fractional_scale_manager_v1_get_fractional_scale(kit.globals.fractional_scale(), surface_);
        static constexpr wp_fractional_scale_v1_listener kFractional{&MirrorSurface::preferred_scale};
        wp_fractional_scale_v1_add_listener(fractional_, &kFractional, this);
    }
}

MirrorSurface::~MirrorSurface() {
    kit_.seat.forget(surface_);
    held_.clear();
    sync_.reset();
    if (alpha_ != nullptr) wp_alpha_modifier_surface_v1_destroy(alpha_);
    if (fractional_ != nullptr) wp_fractional_scale_v1_destroy(fractional_);
    wp_viewport_destroy(viewport_);
    wl_surface_destroy(surface_);
}

// Only a mirror's surfaces have a listener: the cursor's and a backdrop's
// carry no data, and take no input.
SurfaceInput* MirrorSurface::input_of(wl_surface* surface) noexcept {
    auto* mirror = surface == nullptr ? nullptr : static_cast<MirrorSurface*>(wl_surface_get_user_data(surface));
    return mirror == nullptr ? nullptr : &mirror->input_;
}

void MirrorSurface::preferred_scale(void* self, wp_fractional_scale_v1* /*fractional*/, std::uint32_t scale) {
    static_cast<MirrorSurface*>(self)->take_scale(scale / kFractionalDenominator);
}

void MirrorSurface::preferred_buffer_scale(void* self, wl_surface* /*surface*/, std::int32_t factor) {
    auto* surface = static_cast<MirrorSurface*>(self);
    if (surface->fractional_ == nullptr) surface->take_scale(std::max(1, factor));
}

void MirrorSurface::entered_output(void* /*self*/, wl_surface* /*surface*/, wl_output* /*output*/) {}
void MirrorSurface::left_output(void* /*self*/, wl_surface* /*surface*/, wl_output* /*output*/) {}
void MirrorSurface::preferred_buffer_transform(void* /*self*/, wl_surface* /*surface*/, std::uint32_t /*transform*/) {}

void MirrorSurface::take_scale(double scale) {
    if (scale <= 0.0 || scale == scale_) return;
    scale_ = scale;
    RWIN_DEBUG("surface", "scale {:.3f}", scale_);
    commit();
    rescaled();
}

void MirrorSurface::fall_back_to(double scale) {
    if (scale <= 0.0 || scale == fallback_scale_) return;
    fallback_scale_ = scale;
    if (scale_ > 0.0) return;
    commit();
    rescaled();
}

ViewMapping MirrorSurface::mapping() const noexcept {
    const Size picture = picture_.empty() ? Size{1, 1} : picture_;
    return fitted_.has_value() ? ViewMapping::stretched(picture, *fitted_) : ViewMapping::one_to_one(picture, scale());
}

void MirrorSurface::fit_one_to_one() {
    if (!fitted_.has_value()) return;
    fitted_.reset();
    commit();
}

void MirrorSurface::fit_to(Size logical) {
    if (fitted_ == logical) return;
    fitted_ = logical;
    commit();
}

void MirrorSurface::shape_input(std::vector<Box> picture_rects) {
    if (picture_rects == input_rects_) return;
    input_rects_ = std::move(picture_rects);
    commit();
}

void MirrorSurface::take_no_input(bool none) {
    if (none == no_input_) return;
    no_input_ = none;
    commit();
}

void MirrorSurface::set_opacity(std::uint8_t alpha) {
    if (kit_.globals.alpha_modifier() == nullptr) return;
    if (alpha_ == nullptr) {
        if (alpha == std::numeric_limits<std::uint8_t>::max()) return;
        alpha_ = wp_alpha_modifier_v1_get_surface(kit_.globals.alpha_modifier(), surface_);
    }
    const auto factor = static_cast<std::uint32_t>(static_cast<std::uint64_t>(alpha) * 0xFFFF'FFFFu / 255u);
    wp_alpha_modifier_surface_v1_set_multiplier(alpha_, factor);
    commit();
}

bool MirrorSurface::cut_out(std::optional<std::uint32_t> color_key, std::vector<Box> region) {
    PictureCutout cutout{color_key, std::move(region)};
    if (cutout == cutout_) return false;
    cutout_ = std::move(cutout);
    return true;
}

void MirrorSurface::allow_commits() {
    commits_allowed_ = true;
    if (pixels_waiting_ != nullptr) {
        attach_pixels();
    } else if (shared_waiting_.has_value()) {
        try_attach_shared();
    } else {
        commit();
    }
}

void MirrorSurface::commit() {
    if (!commits_allowed_ || attaching_) return;
    apply_view();
    apply_input();
    wl_surface_commit(surface_);
}

void MirrorSurface::apply_view() {
    if (picture_.empty()) return;
    const ViewMapping view = mapping();
    wp_viewport_set_source(viewport_, wl_fixed_from_int(0), wl_fixed_from_int(0),
                           wl_fixed_from_double(view.source_width()), wl_fixed_from_double(view.source_height()));
    wp_viewport_set_destination(viewport_, view.logical().width, view.logical().height);
}

void MirrorSurface::apply_input() {
    if (!no_input_ && input_rects_.empty()) {
        wl_surface_set_input_region(surface_, nullptr);
        return;
    }
    wl_region* region = wl_compositor_create_region(kit_.globals.compositor());
    const ViewMapping view = mapping();
    if (!no_input_) {
        for (const Box& rect : input_rects_) {
            const Box units = view.logical_box(rect);
            wl_region_add(region, units.x, units.y, units.width, units.height);
        }
    }
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
}

void MirrorSurface::show(const PixelsView& pixels) {
    // A surface going back to pixels from the GPU is no longer explicitly
    // synced: a shared-memory buffer cannot be.
    drop_sync_objects();
    shared_waiting_.reset();
    fence_watch_.reset();
    const Size size{pixels.width, pixels.height};
    ShmBuffer* buffer = shm_.free_buffer(size);
    if (buffer == nullptr) {
        RWIN_TRACE("surface", "every buffer is held: a frame is dropped");
        return;
    }
    const std::span<std::uint8_t> into = buffer->pixels();
    const auto row_bytes = static_cast<std::size_t>(pixels.width) * 4;
    for (int row = 0; row < pixels.height; ++row) {
        std::memcpy(into.data() + static_cast<std::size_t>(row) * buffer->stride(),
                    pixels.bgra + static_cast<std::size_t>(row) * pixels.stride, row_bytes);
    }
    cutout_.apply(into, buffer->stride(), size);
    const bool resized = size != picture_;
    picture_ = size;
    pixels_waiting_ = buffer;
    pixels_stamps_ = FrameStamps{};
    pixels_stamps_.note_now(FrameStamps::Stage::Shown);
    if (resized) announce_resize();
    if (commits_allowed_) attach_pixels();
}

void MirrorSurface::attach_pixels() {
    ShmBuffer* buffer = std::exchange(pixels_waiting_, nullptr);
    if (buffer == nullptr) return;
    buffer->attached();
    commit_attached(buffer->buffer(), pixels_stamps_);
}

// The newest frame wins: one that comes while another still waits for its
// buffer or its copy replaces it, and the replaced one's slot goes back.
void MirrorSurface::show(const SharedFrame& frame) {
    if (!cutout_.none()) {
        shared_refused(cutout_.color_key.has_value() ? "a colour key, cut out on pixels"
                                                     : "a window region, cut out on pixels");
        return;
    }
    pixels_waiting_ = nullptr;
    fence_watch_.reset();
    shared_waiting_ = frame;
    shared_waiting_->stamps.note_now(FrameStamps::Stage::Shown);
    try_attach_shared();
}

void MirrorSurface::try_attach_shared() {
    if (!shared_waiting_.has_value()) return;
    if (kit_.globals.dmabuf() == nullptr) {
        shared_waiting_.reset();
        shared_refused("the compositor takes no dma-bufs");
        return;
    }
    wl_buffer* buffer = dmabufs_.buffer_for(shared_waiting_->memory);
    if (buffer == nullptr || !commits_allowed_) return;
    if (explicitly_synced()) {
        attach_shared(buffer);
        return;
    }
    const int fence = shared_waiting_->fence->get();
    if (fence < 0 || signalled(fence)) {
        attach_shared(buffer);
        return;
    }
    fence_watch_ = kit_.loop.watch(fence, POLLIN, [this, buffer](short) {
        fence_watch_.reset();
        attach_shared(buffer);
    });
}

bool MirrorSurface::explicitly_synced() {
    if (!kit_.sync.usable()) return false;
    if (sync_ == nullptr) {
        sync_ = std::make_unique<SyncObjects>(kit_.sync, surface_);
        if (!sync_->acquire.valid() || !sync_->release.valid()) {
            RWIN_WARN("sync", "no timelines for a surface: its frames are waited for here");
            drop_sync_objects();
            return false;
        }
    }
    return true;
}

void MirrorSurface::drop_sync_objects() { sync_.reset(); }

void MirrorSurface::attach_shared(wl_buffer* buffer) {
    if (!shared_waiting_.has_value()) return;
    SharedFrame frame = std::move(*shared_waiting_);
    shared_waiting_.reset();
    frame.stamps.note_now(FrameStamps::Stage::Acquired);
    // Said before the sync points are: whatever is said with the new size
    // goes out with this buffer's commit, and no commit comes between.
    if (frame.size() != picture_) {
        picture_ = frame.size();
        announce_resize();
    }
    Held held{buffer, std::move(frame.lease), UniqueFd{}, nullptr};
    if (sync_ != nullptr) {
        const std::uint64_t point = ++sync_->point;
        const int fence = frame.fence->get();
        if (fence < 0) {
            sync_->acquire.signal_now(point);
        } else if (!sync_->acquire.signal_with(fence, point)) {
            // The copy is waited for here instead; the frame is late, not lost.
            pollfd ready{fence, POLLIN, 0};
            static_cast<void>(::poll(&ready, 1, -1));
            sync_->acquire.signal_now(point);
        }
        const auto high = static_cast<std::uint32_t>(point >> 32);
        const auto low = static_cast<std::uint32_t>(point & 0xFFFF'FFFFu);
        wp_linux_drm_syncobj_surface_v1_set_acquire_point(sync_->synced, sync_->acquire.timeline(), high, low);
        wp_linux_drm_syncobj_surface_v1_set_release_point(sync_->synced, sync_->release.timeline(), high, low);
        held.released = sync_->release.when_signalled(point);
    }
    commit_attached(buffer, frame.stamps);
    // Let go of whatever the compositor held of this buffer before: it is
    // the same slot, which the guest does not fill again until let go of.
    std::erase_if(held_, [buffer](const Held& earlier) { return earlier.buffer == buffer; });
    held_.push_back(std::move(held));
    Held& kept = held_.back();
    if (kept.released.valid()) {
        const int released = kept.released.get();
        kept.watch = kit_.loop.watch(released, POLLIN, [this, released](short) {
            std::erase_if(held_, [released](const Held& done) { return done.released.get() == released; });
        });
    }
}

void MirrorSurface::buffer_made(const SurfaceMemory& memory, wl_buffer* buffer, const std::string& why) {
    if (buffer == nullptr) {
        if (shared_waiting_.has_value() && shared_waiting_->memory.get() == &memory) shared_waiting_.reset();
        RWIN_WARN("surface", "{}", why);
        shared_refused(why);
        return;
    }
    if (shared_waiting_.has_value() && shared_waiting_->memory.get() == &memory) try_attach_shared();
}

// Without explicit sync, the compositor's release of a buffer is when its
// slot goes back; with it, the release point says so instead.
void MirrorSurface::buffer_released(wl_buffer* buffer) {
    std::erase_if(held_, [buffer](const Held& held) { return held.buffer == buffer && !held.released.valid(); });
}

// Whoever hears of a new size sets what goes with it; the commit carrying
// the picture of that size carries it too. Committed before, the viewport
// would be cut from a buffer the old size.
void MirrorSurface::announce_resize() {
    attaching_ = true;
    picture_resized();
    attaching_ = false;
}

void MirrorSurface::commit_attached(wl_buffer* buffer, const FrameStamps& stamps) {
    wl_surface_attach(surface_, buffer, 0, 0);
    wl_surface_damage_buffer(surface_, 0, 0, std::numeric_limits<std::int32_t>::max(),
                             std::numeric_limits<std::int32_t>::max());
    FrameStamps committed = stamps;
    committed.note_now(FrameStamps::Stage::Committed);
    presentation_.ask(surface_, committed);
    commit();
    if (mapped_) return;
    mapped_ = true;
    first_mapped();
}

}  // namespace rwin::host
