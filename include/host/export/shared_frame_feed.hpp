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

class SharedFrameFeed {
public:
    SharedFrameFeed(EventLoop& loop, GuestLink& link);
    ~SharedFrameFeed();

    SharedFrameFeed(const SharedFrameFeed&) = delete;
    SharedFrameFeed& operator=(const SharedFrameFeed&) = delete;
    SharedFrameFeed(SharedFrameFeed&&) = delete;
    SharedFrameFeed& operator=(SharedFrameFeed&&) = delete;

    void open(const std::string& path, const CompositorGpu& compositor);
    [[nodiscard]] bool offered() const noexcept { return socket_.has_value(); }

    void give_up(const std::string& why);
    void pixels_for(WindowId window, const std::string& why);

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
    std::optional<std::uint64_t> ask(ExportRequest::Operation operation, std::uint64_t surface);
    std::optional<std::uint64_t> ask(const ExportRequest& request);
    [[nodiscard]] WindowRing* ring_of(WindowId window, std::uint32_t ring);
    [[nodiscard]] std::optional<WindowId> window_of(std::uint64_t surface) const;

    EventLoop& loop_;
    GuestLink& link_;
    std::optional<ExportSocket> socket_;
    std::unique_ptr<EventLoop::Watch> answers_;
    Deferred later_;
    std::deque<Asked> asked_;
    std::map<std::uint64_t, PendingFrame> syncing_;
    std::map<std::uint64_t, WindowRing> rings_;
    std::unordered_set<WindowId> pixels_only_;
    std::uint64_t next_tag_ = 0;
    bool other_gpu_ = false;
    bool twin_refusal_said_ = false;
    std::map<std::uint64_t, int> export_asks_;
    static constexpr int kMostExportAsks = 500;
    static constexpr int kExportRetryMs = 2;
    FrameTiming timing_{"feed"};
    GuestClock clock_;
    LatencyLog latency_;
    DisplayLock lock_;
    std::uint32_t guest_microhz_ = 0;
};

}
