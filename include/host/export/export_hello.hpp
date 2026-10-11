#pragma once

#include "host/export/export_gpu.hpp"
#include "host/export/export_socket.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace rwin::host {

// A connection to the GPU device's export socket that the device has
// greeted: it answered hello, it exports, and it named the GPU its
// surfaces are on.
class ExportHello {
public:
    // Connected to ``path`` and greeted under ``tag``; nothing, with
    // ``why`` saying what failed, otherwise.
    [[nodiscard]] static std::optional<ExportHello> greet(const std::string& path, std::uint64_t tag,
                                                          std::string& why);

    [[nodiscard]] const ExportGpu& gpu() const noexcept { return gpu_; }
    [[nodiscard]] ExportSocket& socket() noexcept { return socket_; }

private:
    ExportHello(ExportSocket socket, const ExportGpu& gpu) noexcept : socket_(std::move(socket)), gpu_(gpu) {}

    ExportSocket socket_;
    ExportGpu gpu_;
};

}  // namespace rwin::host
