#pragma once

#include "host/export/copy_wait.hpp"
#include "host/export/export_request.hpp"
#include "host/export/export_socket.hpp"
#include "host/export/shared_frame.hpp"
#include "host/export/window_ring.hpp"

#include <QObject>

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>

class QSocketNotifier;

namespace rwin {
struct FrameInSlot;
struct SlotRing;
}

namespace rwin::host {

class GuestLink;
class SlotLease;

// The guest's frames that stay on the GPU, made ready to draw: each
// window's ring of shared surfaces exported once from the GPU device's
// export socket (accel-virt's docs/external-export.md), and each frame the
// guest names in it waited for until the device has run the guest's copy -
// then handed on with the fence that covers that copy on the GPU.
//
// The guest is told at its hello whether to send frames this way
// (FrameRoute): only when the export socket answers and its GPU is the one
// windows here are drawn on. Anything that goes wrong after - the
// connection lost, a surface not exported, a frame that cannot be imported
// - tells it to send pixels instead, for good: a mirror that shows frames
// as pixels is a mirror, and one that waits on a broken path is not.
//
// Nothing is copied here and no pixel is touched; what crosses this class
// is descriptors and counts.
class SharedFrameFeed : public QObject {
    Q_OBJECT
public:
    explicit SharedFrameFeed(GuestLink& link, QObject* parent = nullptr);
    ~SharedFrameFeed() override;

    SharedFrameFeed(const SharedFrameFeed&) = delete;
    SharedFrameFeed& operator=(const SharedFrameFeed&) = delete;
    SharedFrameFeed(SharedFrameFeed&&) = delete;
    SharedFrameFeed& operator=(SharedFrameFeed&&) = delete;

    // Connects to the export socket at ``path`` and checks the GPU it
    // names: offered() from then on, unless either fails, and then frames
    // come as pixels, with a line saying why.
    void open(const std::string& path);
    [[nodiscard]] bool offered() const noexcept { return socket_.has_value(); }

    // Something here cannot draw the frames from the GPU (``why``): they
    // come as pixels from here on.
    void give_up(const std::string& why);

Q_SIGNALS:
    void frame_ready(quint64 window, const rwin::host::SharedFrame& frame);

private:
    struct Asked {
        ExportRequest::Operation operation;
        std::uint64_t surface;
        std::uint64_t tag;
    };
    struct PendingFrame {
        quint64 window;
        std::uint32_t ring;
        std::uint32_t slot;
        std::uint64_t order;
        CopyWait wait;
        std::shared_ptr<const SlotLease> lease;
    };

    void offer();
    void take_ring(const SlotRing& ring);
    void take_frame(const FrameInSlot& frame);
    void drop_window(quint64 window);
    void read_answers();
    void take_answer(ExportSocket::Received received);
    void exported(const ExportAnswer& answer, UniqueFd dma_buf);
    void synced(const ExportAnswer& answer, UniqueFd fence, PendingFrame frame);
    void revoked(const ExportAnswer& answer);
    void ask_sync(PendingFrame frame);
    // Sent, and its answer awaited in order; nothing once given up.
    std::optional<std::uint64_t> ask(ExportRequest::Operation operation, std::uint64_t surface);
    [[nodiscard]] WindowRing* ring_of(quint64 window, std::uint32_t ring);

    GuestLink& link_;
    std::optional<ExportSocket> socket_;
    QSocketNotifier* notifier_ = nullptr;
    // Answers come in the order requests went, on the one connection.
    std::deque<Asked> asked_;
    std::map<std::uint64_t, PendingFrame> syncing_;
    std::map<quint64, WindowRing> rings_;
    std::uint64_t next_tag_ = 0;
    // How often a surface's export has found nothing yet, by surface.
    std::map<std::uint64_t, int> export_asks_;
    // An export asked again this often, this far apart, before the GPU is
    // given up: a second for the guest's batch making the surface to run.
    static constexpr int kMostExportAsks = 500;
    static constexpr int kExportRetryMs = 2;
};

}  // namespace rwin::host
