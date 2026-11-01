#pragma once

#include "host/export/copy_wait.hpp"
#include "host/export/export_request.hpp"
#include "host/export/export_socket.hpp"
#include "host/export/shared_frame.hpp"
#include "host/export/pci_address.hpp"
#include "host/export/window_ring.hpp"
#include "host/geometry/size.hpp"
#include "host/latency/display_lock.hpp"
#include "host/latency/guest_clock.hpp"
#include "host/latency/latency_log.hpp"
#include "host/loop/deferred.hpp"
#include "host/loop/event_loop.hpp"
#include "host/loop/signal.hpp"
#include "logging/frame_timing.hpp"
#include "proto/window_id.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

namespace rwin {
struct FrameInSlot;
struct SlotRing;
}

namespace rwin::host {

class CompositorGpu;
class GuestLink;
class SlotLease;

// The guest's frames that stay on the GPU, made ready to draw: each
// window's ring of shared surfaces exported once from the GPU device's
// export socket (accel-virt's docs/external-export.md), and each frame the
// guest names in it waited for until the device has run the guest's copy -
// then handed on with the fence that covers that copy on the GPU.
//
// The guest is told at its hello whether to send frames this way
// (FrameRoute): only when the export socket answers and the compositor can
// take what it exports. On the GPU the compositor composes on, the surfaces
// come in their own memory, in the layout their driver keeps with it. On
// another, each export asks for the device's copy of the surface in linear
// system memory (ExportRequest::kOtherGpu), made on the surfaces' GPU on
// every sync, which the compositor's GPU can read: the frame crosses the bus
// once and never the guest's network. A window whose surfaces cannot be
// exported or drawn here has its frames sent as pixels from then on (WindowPixels),
// and the others keep theirs; only the export connection itself failing
// sends every window back to pixels, for good: a mirror that shows frames
// as pixels is a mirror, and one that waits on a broken path is not.
//
// Nothing is copied here and no pixel is touched; what crosses this class
// is descriptors and counts.
class SharedFrameFeed {
public:
    SharedFrameFeed(EventLoop& loop, GuestLink& link);
    ~SharedFrameFeed();

    SharedFrameFeed(const SharedFrameFeed&) = delete;
    SharedFrameFeed& operator=(const SharedFrameFeed&) = delete;
    SharedFrameFeed(SharedFrameFeed&&) = delete;
    SharedFrameFeed& operator=(SharedFrameFeed&&) = delete;

    // Connects to the export socket at ``path`` and checks the GPU it
    // names against the one ``compositor`` composes on (nothing where it
    // does not say, and a buffer it cannot take sends that window back to
    // pixels): offered() from then on, unless either fails, and then frames
    // come as pixels, with a line saying why. On another GPU than the
    // surfaces', the frames cross in linear system memory, and only if the
    // compositor takes linear ARGB8888.
    void open(const std::string& path, const CompositorGpu& compositor);
    [[nodiscard]] bool offered() const noexcept { return socket_.has_value(); }

    // Something here cannot draw the frames from the GPU (``why``): they
    // come as pixels from here on.
    void give_up(const std::string& why);
    // ``window``'s frames cannot be drawn from the GPU (``why``): its own
    // come as pixels from here on.
    void pixels_for(WindowId window, const std::string& why);

    // A frame from here reached the screen: said in the log with the rest
    // (LatencyLog), and, while ``active`` - the surface it was shown on has
    // the keyboard - what times the guest's display (DisplayLock).
    void shown(const FrameStamps& stamps, bool active);

    Signal<WindowId, const SharedFrame&> frame_ready;

private:
    struct Asked {
        ExportRequest::Operation operation;
        std::uint64_t surface;
        std::uint64_t tag;
    };
    struct PendingFrame {
        WindowId window;
        std::uint32_t ring;
        std::uint32_t slot;
        // The picture, at the surface's top left.
        Size picture;
        std::uint64_t order;
        CopyWait wait;
        std::shared_ptr<const SlotLease> lease;
        FrameStamps stamps;
    };

    void offer();
    void take_ring(const SlotRing& ring);
    void take_frame(const FrameInSlot& frame);
    void drop_window(WindowId window);
    void read_answers();
    void take_answer(ExportSocket::Received received);
    void exported(const ExportAnswer& answer, UniqueFd dma_buf);
    void synced(const ExportAnswer& answer, UniqueFd fence, PendingFrame frame);
    void revoked(const ExportAnswer& answer);
    void ask_sync(PendingFrame frame);
    // Sent, and its answer awaited in order; nothing once given up.
    std::optional<std::uint64_t> ask(ExportRequest::Operation operation, std::uint64_t surface);
    std::optional<std::uint64_t> ask(const ExportRequest& request);
    [[nodiscard]] WindowRing* ring_of(WindowId window, std::uint32_t ring);
    // The window whose ring ``surface`` is one of.
    [[nodiscard]] std::optional<WindowId> window_of(std::uint64_t surface) const;

    EventLoop& loop_;
    GuestLink& link_;
    std::optional<ExportSocket> socket_;
    std::unique_ptr<EventLoop::Watch> answers_;
    Deferred later_;
    // Answers come in the order requests went, on the one connection.
    std::deque<Asked> asked_;
    std::map<std::uint64_t, PendingFrame> syncing_;
    // By WindowId::key.
    std::map<std::uint64_t, WindowRing> rings_;
    // Sent to pixels alone (pixels_for): a ring still on its way for one is
    // not taken.
    std::unordered_set<WindowId> pixels_only_;
    std::uint64_t next_tag_ = 0;
    // The compositor is on another GPU than the surfaces: exports and syncs
    // ask for the copy in system memory.
    bool other_gpu_ = false;
    // The device said it cannot make that copy, and this side said so.
    bool twin_refusal_said_ = false;
    // How often a surface's export has found nothing yet, by surface.
    std::map<std::uint64_t, int> export_asks_;
    // An export asked again this often, this far apart, before the GPU is
    // given up: a second for the guest's batch making the surface to run.
    static constexpr int kMostExportAsks = 500;
    static constexpr int kExportRetryMs = 2;
    FrameTiming timing_{"feed"};
    GuestClock clock_;
    LatencyLog latency_;
    DisplayLock lock_;
    // The rate the guest's display blanks at, as the last sync said; 0 before
    // one did, and from a device that does not say.
    std::uint32_t guest_microhz_ = 0;
};

}  // namespace rwin::host
