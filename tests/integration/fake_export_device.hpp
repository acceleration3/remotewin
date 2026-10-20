#pragma once

#include "host/export/export_answer.hpp"
#include "host/export/pci_address.hpp"
#include "host/export/unique_fd.hpp"
#include "host/loop/event_loop.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rwin::integration {

// The GPU device's export socket, played (accel-virt's
// docs/external-export.md): a SOCK_SEQPACKET socket the host under test
// connects to, which answers hello naming the compositor's GPU, exports a
// surface the played guest made as memory of its size, and syncs a slot
// with every copy into it run and a signalled fence. The memory is no
// dma-buf a compositor can show; it is enough for a frame to reach the
// host's mirror, which is where a frame it cannot show is refused.
class FakeExportDevice {
public:
    FakeExportDevice(host::EventLoop& loop, std::string path, std::optional<host::PciAddress> gpu);

    [[nodiscard]] bool listening() const noexcept { return listener_.valid(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // The guest made a surface of this size (a slot of a ring), and copied
    // into it once more.
    void made(std::uint64_t surface, std::uint32_t width, std::uint32_t height);
    void copied(std::uint64_t surface);
    // How many surfaces the host had exported.
    [[nodiscard]] std::size_t exports() const noexcept { return exports_; }

private:
    struct Surface {
        std::uint64_t id = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint64_t copies = 0;
    };

    void accept();
    void read_requests();
    void answer(std::span<const std::uint8_t> request);
    void send(std::uint32_t operation, host::ExportAnswer::Status status, std::uint64_t surface, std::uint64_t tag,
              std::span<const std::uint8_t> body, int descriptor);
    [[nodiscard]] Surface* surface(std::uint64_t id);

    host::EventLoop& loop_;
    std::string path_;
    std::optional<host::PciAddress> gpu_;
    host::UniqueFd listener_;
    host::UniqueFd connection_;
    std::unique_ptr<host::EventLoop::Watch> accepting_;
    std::unique_ptr<host::EventLoop::Watch> reading_;
    std::vector<Surface> surfaces_;
    std::size_t exports_ = 0;
};

}  // namespace rwin::integration
