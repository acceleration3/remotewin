#include "host/export/shared_frame_feed.hpp"

#include "host/export/export_hello.hpp"
#include "host/export/slot_lease.hpp"
#include "host/export/surface_memory.hpp"
#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"

#include <poll.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace rwin::host {

SharedFrameFeed::SharedFrameFeed(EventLoop& loop, GuestLink& link)
    : loop_(loop), link_(link), later_(loop), clock_(loop, link) {
    link_.connected.connect([this] { offer(); });
    link_.ring_announced.connect([this](const SlotRing& ring) { take_ring(ring); });
    link_.frame_in_slot.connect([this](const FrameInSlot& frame) { take_frame(frame); });
    link_.window_gone.connect([this](WindowId window) {
        pixels_only_.erase(window);
        drop_window(window);
    });
    // A window the guest sends as pixels has no ring any more.
    link_.frame_arrived.connect([this](WindowId window, const PixelsView&) { drop_window(window); });
}

// Leases let go of after the socket: what they free is the guest's.
SharedFrameFeed::~SharedFrameFeed() = default;

void SharedFrameFeed::open(const std::string& path, const std::optional<PciAddress>& composing) {
    std::string why;
    std::optional<ExportHello> hello = ExportHello::greet(path, ++next_tag_, why);
    if (!hello.has_value()) {
        RWIN_WARN("export", "frames come as pixels: {}", why);
        return;
    }
    const ExportGpu gpu = hello->gpu();
    if (!gpu.pci.has_value()) {
        RWIN_WARN("export", "frames come as pixels: the GPU device's GPU is on no PCI bus");
        return;
    }
    if (composing.has_value() && *composing != *gpu.pci) {
        RWIN_WARN("export", "frames come as pixels: the guest's surfaces are on {}, the compositor composes on {}",
                  gpu.pci->to_string(), composing->to_string());
        return;
    }
    socket_ = std::move(hello->socket());
    answers_ = loop_.watch(socket_->descriptor(), POLLIN, [this](short) { read_answers(); });
    RWIN_INFO("export", "frames stay on the GPU: '{}', {}", path, gpu.pci->to_string());
}

void SharedFrameFeed::offer() {
    link_.send_route(offered());
}

void SharedFrameFeed::give_up(const std::string& why) {
    if (!offered()) return;
    RWIN_WARN("export", "frames come as pixels from here on: {}", why);
    link_.send_route(false);
    answers_.reset();
    later_.clear();
    socket_.reset();
    asked_.clear();
    syncing_.clear();
    rings_.clear();
    export_asks_.clear();
    pixels_only_.clear();
}

void SharedFrameFeed::pixels_for(WindowId window, const std::string& why) {
    if (!offered() || !pixels_only_.insert(window).second) return;
    RWIN_WARN("export", "{}'s frames come as pixels from here on: {}", window, why);
    link_.send_window_pixels(window);
    drop_window(window);
}

void SharedFrameFeed::take_ring(const SlotRing& ring) {
    if (!offered() || pixels_only_.contains(ring.window)) return;
    drop_window(ring.window);
    rings_.insert_or_assign(ring.window.key(), WindowRing{ring});
    RWIN_DEBUG("export", "{}: ring {} of {}x{}", ring.window, ring.ring, ring.width, ring.height);
    for (const std::uint64_t surface : ring.surfaces) {
        if (!ask(ExportRequest::Operation::Export, surface).has_value()) return;
    }
}

void SharedFrameFeed::drop_window(WindowId window) {
    const auto known = rings_.find(window.key());
    if (known == rings_.end()) return;
    const std::array<std::uint64_t, SlotRing::kSlots> surfaces = known->second.surfaces();
    rings_.erase(known);
    for (const std::uint64_t surface : surfaces) {
        if (!ask(ExportRequest::Operation::Release, surface).has_value()) return;
    }
}

WindowRing* SharedFrameFeed::ring_of(WindowId window, std::uint32_t ring) {
    const auto known = rings_.find(window.key());
    return known != rings_.end() && known->second.ring() == ring ? &known->second : nullptr;
}

std::optional<WindowId> SharedFrameFeed::window_of(std::uint64_t surface) const {
    for (const auto& [key, ring] : rings_) {
        if (ring.slot_of(surface).has_value()) return WindowId::of_key(key);
    }
    return std::nullopt;
}

// A frame in a ring this side does not hold - one replaced, or one named
// after this side gave up - is freed at once: the guest waits for it.
void SharedFrameFeed::take_frame(const FrameInSlot& frame) {
    auto lease = std::make_shared<const SlotLease>(link_, SlotFree{frame.window, frame.ring, frame.slot});
    WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    const auto order = ring->named();
    timing_.note("received", order, 0, frame.window.key());
    FrameStamps stamps;
    stamps.note_now(FrameStamps::Stage::Received);
    stamps.note(FrameStamps::Stage::Composed, clock_.to_host(frame.composed).value_or(0));
    stamps.note(FrameStamps::Stage::Taken, clock_.to_host(frame.taken).value_or(0));
    stamps.note(FrameStamps::Stage::Copied, clock_.to_host(frame.copied).value_or(0));
    ask_sync(PendingFrame{frame.window, frame.ring, frame.slot, order, CopyWait{frame.copies},
                          std::move(lease), stamps});
}

void SharedFrameFeed::ask_sync(PendingFrame frame) {
    const WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    const std::optional<std::uint64_t> tag = ask(ExportRequest::Operation::Sync, ring->surface(frame.slot));
    if (tag.has_value()) syncing_.insert_or_assign(*tag, std::move(frame));
}

std::optional<std::uint64_t> SharedFrameFeed::ask(ExportRequest::Operation operation, std::uint64_t surface) {
    return ask(ExportRequest{operation, surface, 0});
}

std::optional<std::uint64_t> SharedFrameFeed::ask(const ExportRequest& request) {
    if (!offered()) return std::nullopt;
    ExportRequest tagged = request;
    tagged.tag = ++next_tag_;
    if (!socket_->send(tagged)) {
        give_up("the GPU device's export socket took no more requests");
        return std::nullopt;
    }
    asked_.push_back(Asked{tagged.operation, tagged.surface, tagged.tag});
    return tagged.tag;
}

// Only the application the user is in times the guest's display: there is
// one display clock, and a mirror on another output would put it in phase
// with that one instead.
void SharedFrameFeed::shown(const FrameStamps& stamps, bool active) {
    latency_.take(stamps);
    if (active_ && !active) lock_.forget();
    active_ = active;
    if (!active) return;
    const std::optional<DisplayLock::Timing> timing = lock_.take(stamps, now_ns());
    if (!timing.has_value()) return;
    RWIN_DEBUG("latency", "asking the guest's display for {:.3f} Hz, blanking {:.2f} ms before this output does",
               timing->refresh_millihz / 1000.0, static_cast<double>(lock_.ahead()) / 1e6);
    ExportRequest request{ExportRequest::Operation::Timing, 0, 0};
    request.refresh_millihz = timing->refresh_millihz;
    request.blank = static_cast<std::uint64_t>(timing->blank);
    static_cast<void>(ask(request));
}

void SharedFrameFeed::read_answers() {
    while (offered()) {
        bool gone = false;
        std::optional<ExportSocket::Received> received = socket_->receive(gone);
        if (gone) {
            give_up("the GPU device's export connection ended");
            return;
        }
        if (!received.has_value()) return;
        take_answer(std::move(*received));
    }
}

void SharedFrameFeed::take_answer(ExportSocket::Received received) {
    const ExportAnswer& answer = received.answer;
    if (answer.operation == ExportAnswer::kRevoked) {
        revoked(answer);
        return;
    }
    if (asked_.empty() || asked_.front().tag != answer.tag
        || static_cast<std::uint32_t>(asked_.front().operation) != answer.operation) {
        give_up("the GPU device answered something not asked (operation " + std::to_string(answer.operation)
                + ", tag " + std::to_string(answer.tag) + ")");
        return;
    }
    const Asked asked = asked_.front();
    asked_.pop_front();
    switch (asked.operation) {
        case ExportRequest::Operation::Export:
            exported(answer, std::move(received.descriptor));
            return;
        case ExportRequest::Operation::Sync: {
            auto pending = syncing_.find(asked.tag);
            if (pending == syncing_.end()) return;
            PendingFrame frame = std::move(pending->second);
            syncing_.erase(pending);
            synced(answer, std::move(received.descriptor), std::move(frame));
            return;
        }
        case ExportRequest::Operation::Timing:
            if (!answer.ok()) RWIN_DEBUG("export", "the GPU device did not take the timing: {}", to_string(answer.status));
            return;
        case ExportRequest::Operation::Hello:
        case ExportRequest::Operation::Release:
            return;
    }
}

void SharedFrameFeed::exported(const ExportAnswer& answer, UniqueFd dma_buf) {
    // The guest names a ring as soon as it has made its textures, and they
    // reach the GPU device only once the guest's batch making them has run
    // there: an export asked first finds nothing yet. Asked again a moment
    // later, while the surface is still one of a ring's - one replaced
    // meanwhile is nothing to wait for - rather than giving up the GPU for
    // the whole session, which left Steam's window crossing as pixels.
    if (answer.status == ExportAnswer::Status::NotFound) {
        const bool held = std::any_of(rings_.begin(), rings_.end(), [&](const auto& entry) {
            return entry.second.slot_of(answer.surface).has_value();
        });
        if (!held) {
            export_asks_.erase(answer.surface);
            return;
        }
        if (++export_asks_[answer.surface] < kMostExportAsks) {
            later_.after(std::chrono::milliseconds(kExportRetryMs), [this, surface = answer.surface] {
                static_cast<void>(ask(ExportRequest::Operation::Export, surface));
            });
            return;
        }
    }
    export_asks_.erase(answer.surface);
    const std::optional<WindowId> window = window_of(answer.surface);
    if (!window.has_value()) {
        // Its ring was replaced while it was being exported.
        if (answer.ok()) static_cast<void>(ask(ExportRequest::Operation::Release, answer.surface));
        return;
    }
    if (!answer.ok()) {
        pixels_for(*window, "the GPU device did not export surface " + std::to_string(answer.surface) + ": "
                                + to_string(answer.status));
        return;
    }
    const ExportedLayout layout = answer.layout();
    if (layout.fourcc != ExportedLayout::kArgb8888 || layout.width == 0 || layout.height == 0 || !dma_buf.valid()) {
        pixels_for(*window,
                   "surface " + std::to_string(answer.surface) + " was exported as something other than B8G8R8A8");
        return;
    }
    WindowRing& ring = rings_.at(window->key());
    ring.exported(*ring.slot_of(answer.surface),
                  std::make_shared<const SurfaceMemory>(answer.surface, layout, std::move(dma_buf)));
}

void SharedFrameFeed::synced(const ExportAnswer& answer, UniqueFd fence, PendingFrame frame) {
    WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    if (!answer.ok()) {
        RWIN_DEBUG("export", "{}: slot {} not synced: {}", frame.window, frame.slot, to_string(answer.status));
        return;
    }
    switch (frame.wait.answered(answer.copies())) {
        case CopyWait::Verdict::Ready: {
            const std::shared_ptr<const SurfaceMemory>& memory = ring->memory(frame.slot);
            if (memory == nullptr || !fence.valid() || !ring->show(frame.order)) return;
            timing_.note("ready", frame.order, 0, frame.window.key());
            frame.stamps.note_now(FrameStamps::Stage::Ready);
            frame_ready(frame.window, SharedFrame{memory, std::move(frame.lease),
                                                  std::make_shared<UniqueFd>(std::move(fence)), frame.ring,
                                                  frame.order, frame.stamps});
            return;
        }
        case CopyWait::Verdict::AskAgain:
            later_.after(std::chrono::milliseconds(CopyWait::kRetryMs),
                         [this, again = std::make_shared<PendingFrame>(std::move(frame))] { ask_sync(std::move(*again)); });
            return;
        case CopyWait::Verdict::GiveUp:
            RWIN_WARN("export", "{}: copy {} into slot {} never ran (the device has run {}); frame dropped",
                      frame.window, frame.wait.copies(), frame.slot, answer.copies());
            return;
    }
}

void SharedFrameFeed::revoked(const ExportAnswer& answer) {
    if (answer.surface == 0) {
        // The guest makes every window's ring afresh when told again, those
        // it was sending as pixels included: until then each window would
        // stop at its last frame.
        RWIN_WARN("export", "the GPU device was reset; every shared surface is gone, asking for new ones");
        rings_.clear();
        syncing_.clear();
        export_asks_.clear();
        pixels_only_.clear();
        link_.send_route(true);
        return;
    }
    // A ring the guest let go of whole is forgotten, rather than kept until
    // its window goes: its ids may be another surface's next.
    for (auto ring = rings_.begin(); ring != rings_.end();) {
        if (const std::optional<std::uint32_t> slot = ring->second.slot_of(answer.surface)) ring->second.revoke(*slot);
        ring = ring->second.revoked() ? rings_.erase(ring) : std::next(ring);
    }
}

}  // namespace rwin::host
