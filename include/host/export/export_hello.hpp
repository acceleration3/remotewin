#pragma once

#include "host/export/export_gpu.hpp"
#include "host/export/export_socket.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace rwin::host {
class ExportHello {
public:
    [[nodiscard]] static std::optional<ExportHello> greet(const std::string& path, std::uint64_t tag,
                                                          std::string& why);

    [[nodiscard]] const ExportGpu& gpu() const noexcept { return gpu_; }
    [[nodiscard]] ExportSocket& socket() noexcept { return socket_; }

private:
    ExportHello(ExportSocket socket, const ExportGpu& gpu) noexcept : socket_(std::move(socket)), gpu_(gpu) {}

    ExportSocket socket_;
    ExportGpu gpu_;
};

}
