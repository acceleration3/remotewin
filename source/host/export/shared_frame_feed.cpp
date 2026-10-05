#include "host/export/shared_frame_feed.hpp"

#include "host/export/rendering_gpu.hpp"
#include "host/export/slot_lease.hpp"
#include "host/export/surface_memory.hpp"
#include "host/link/guest_link.hpp"
#include "logging/logger.hpp"
#include "proto/frame_in_slot.hpp"
#include "proto/slot_free.hpp"
#include "proto/slot_ring.hpp"

#include <QImage>
#include <QSocketNotifier>
#include <QTimer>

#include <iterator>
#include <utility>

namespace rwin::host {

namespace {

// How long the device has to answer hello: it answers at once, from a
// thread of its own.
constexpr int kHelloMs = 2000;

}  // namespace

SharedFrameFeed::SharedFrameFeed(GuestLink& link, QObject* parent) : QObject(parent), link_(link) {
    connect(&link_, &GuestLink::connected, this, &SharedFrameFeed::offer);
    connect(&link_, &GuestLink::ring_announced, this, &SharedFrameFeed::take_ring);
    connect(&link_, &GuestLink::frame_in_slot, this, &SharedFrameFeed::take_frame);
    connect(&link_, &GuestLink::window_gone, this, &SharedFrameFeed::drop_window);
    // A window the guest sends as pixels has no ring any more.
    connect(&link_, &GuestLink::frame_arrived, this, [this](quint64 window, const QImage&) { drop_window(window); });
}

// Leases let go of after the socket: what they free is the guest's.
SharedFrameFeed::~SharedFrameFeed() = default;

void SharedFrameFeed::open(const std::string& path) {
    std::string why;
    std::optional<ExportSocket> socket = ExportSocket::connect(path, why);
    if (!socket.has_value()) {
        RWIN_WARN("export", "frames come as pixels: {}", why);
        return;
    }
    const std::uint64_t tag = ++next_tag_;
    bool gone = false;
    std::optional<ExportSocket::Received> answer;
    if (socket->send(ExportRequest{ExportRequest::Operation::Hello, 0, tag}) && socket->wait(kHelloMs)) {
        answer = socket->receive(gone);
    }
    if (!answer.has_value() || answer->answer.operation != static_cast<std::uint32_t>(ExportRequest::Operation::Hello)
        || answer->answer.tag != tag) {
        RWIN_WARN("export", "frames come as pixels: the GPU device at '{}' did not answer hello", path);
        return;
    }
    if (!answer->answer.ok()) {
        RWIN_WARN("export", "frames come as pixels: the GPU device at '{}' exports nothing ({})", path,
                  to_string(answer->answer.status));
        return;
    }
    const ExportGpu gpu = answer->answer.gpu();
    if (const std::optional<std::string> refusal = RenderingGpu{}.refusal_for(gpu)) {
        RWIN_WARN("export", "frames come as pixels: {}", *refusal);
        return;
    }
    socket_ = std::move(socket);
    notifier_ = new QSocketNotifier(socket_->descriptor(), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, &SharedFrameFeed::read_answers);
    RWIN_INFO("export", "frames stay on the GPU: '{}', {}, {} surfaces a connection", path,
              gpu.pci->to_string(), gpu.capacity);
}

void SharedFrameFeed::offer() {
    link_.send_route(offered());
}

void SharedFrameFeed::give_up(const std::string& why) {
    if (!offered()) return;
    RWIN_WARN("export", "frames come as pixels from here on: {}", why);
    link_.send_route(false);
    delete notifier_;
    notifier_ = nullptr;
    socket_.reset();
    asked_.clear();
    syncing_.clear();
    rings_.clear();
}

void SharedFrameFeed::take_ring(const SlotRing& ring) {
    if (!offered()) return;
    drop_window(ring.window);
    rings_.insert_or_assign(ring.window, WindowRing{ring});
    RWIN_DEBUG("export", "{:#x}: ring {} of {}x{}", ring.window, ring.ring, ring.width, ring.height);
    for (const std::uint64_t surface : ring.surfaces) {
        if (!ask(ExportRequest::Operation::Export, surface).has_value()) return;
    }
}

void SharedFrameFeed::drop_window(quint64 window) {
    const auto known = rings_.find(window);
    if (known == rings_.end()) return;
    const std::array<std::uint64_t, SlotRing::kSlots> surfaces = known->second.surfaces();
    rings_.erase(known);
    for (const std::uint64_t surface : surfaces) {
        if (!ask(ExportRequest::Operation::Release, surface).has_value()) return;
    }
}

WindowRing* SharedFrameFeed::ring_of(quint64 window, std::uint32_t ring) {
    const auto known = rings_.find(window);
    return known != rings_.end() && known->second.ring() == ring ? &known->second : nullptr;
}

// A frame in a ring this side does not hold - one replaced, or one named
// after this side gave up - is freed at once: the guest waits for it.
void SharedFrameFeed::take_frame(const FrameInSlot& frame) {
    auto lease = std::make_shared<const SlotLease>(link_, SlotFree{frame.window, frame.ring, frame.slot});
    WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    ask_sync(PendingFrame{frame.window, frame.ring, frame.slot, ring->named(), CopyWait{frame.copies},
                          std::move(lease)});
}

void SharedFrameFeed::ask_sync(PendingFrame frame) {
    const WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    const std::optional<std::uint64_t> tag = ask(ExportRequest::Operation::Sync, ring->surface(frame.slot));
    if (tag.has_value()) syncing_.insert_or_assign(*tag, std::move(frame));
}

std::optional<std::uint64_t> SharedFrameFeed::ask(ExportRequest::Operation operation, std::uint64_t surface) {
    if (!offered()) return std::nullopt;
    const std::uint64_t tag = ++next_tag_;
    if (!socket_->send(ExportRequest{operation, surface, tag})) {
        give_up("the GPU device's export socket took no more requests");
        return std::nullopt;
    }
    asked_.push_back(Asked{operation, surface, tag});
    return tag;
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
        case ExportRequest::Operation::Hello:
        case ExportRequest::Operation::Release:
            return;
    }
}

void SharedFrameFeed::exported(const ExportAnswer& answer, UniqueFd dma_buf) {
    if (!answer.ok()) {
        give_up("the GPU device did not export surface " + std::to_string(answer.surface) + ": "
                + to_string(answer.status));
        return;
    }
    const ExportedLayout layout = answer.layout();
    if (layout.fourcc != ExportedLayout::kArgb8888 || layout.width == 0 || layout.height == 0 || !dma_buf.valid()) {
        give_up("surface " + std::to_string(answer.surface) + " was exported as something other than B8G8R8A8");
        return;
    }
    for (auto& [window, ring] : rings_) {
        const std::optional<std::uint32_t> slot = ring.slot_of(answer.surface);
        if (!slot.has_value()) continue;
        ring.exported(*slot, std::make_shared<const SurfaceMemory>(answer.surface, layout, std::move(dma_buf)));
        return;
    }
    // Its ring was replaced while it was being exported.
    static_cast<void>(ask(ExportRequest::Operation::Release, answer.surface));
}

void SharedFrameFeed::synced(const ExportAnswer& answer, UniqueFd fence, PendingFrame frame) {
    WindowRing* ring = ring_of(frame.window, frame.ring);
    if (ring == nullptr) return;
    if (!answer.ok()) {
        RWIN_DEBUG("export", "{:#x}: slot {} not synced: {}", frame.window, frame.slot, to_string(answer.status));
        return;
    }
    switch (frame.wait.answered(answer.copies())) {
        case CopyWait::Verdict::Ready: {
            const std::shared_ptr<const SurfaceMemory>& memory = ring->memory(frame.slot);
            if (memory == nullptr || !fence.valid() || !ring->show(frame.order)) return;
            Q_EMIT frame_ready(frame.window, SharedFrame{memory, std::move(frame.lease),
                                                         std::make_shared<UniqueFd>(std::move(fence)), frame.ring});
            return;
        }
        case CopyWait::Verdict::AskAgain:
            QTimer::singleShot(CopyWait::kRetryMs, this,
                               [this, again = std::move(frame)]() mutable { ask_sync(std::move(again)); });
            return;
        case CopyWait::Verdict::GiveUp:
            RWIN_WARN("export", "{:#x}: copy {} into slot {} never ran (the device has run {}); frame dropped",
                      frame.window, frame.wait.copies(), frame.slot, answer.copies());
            return;
    }
}

void SharedFrameFeed::revoked(const ExportAnswer& answer) {
    if (answer.surface == 0) {
        RWIN_WARN("export", "the GPU device was reset; every shared surface is gone");
        rings_.clear();
        syncing_.clear();
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
