#pragma once

#include "host/export/export_gpu.hpp"
#include "host/export/exported_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rwin::host {
struct ExportAnswer {
    static constexpr std::size_t kBytes = 64;
    static constexpr std::uint32_t kMagic = 0x41585641;
    static constexpr std::uint16_t kMajor = 1;

    static constexpr std::uint32_t kUnreadable = 0;
    static constexpr std::uint32_t kRevoked = 0x80;

    enum class Status : std::uint32_t {
        Ok = 0,
        Malformed = 1,
        NotSupported = 2,
        NotFound = 3,
        NoSpace = 4,
        Unavailable = 5,
    };

    std::uint16_t minor = 0;
    std::uint32_t operation = kUnreadable;
    Status status = Status::Malformed;
    std::uint64_t surface = 0;
    std::uint64_t tag = 0;
    std::array<std::uint8_t, 32> body{};

    [[nodiscard]] static std::optional<ExportAnswer> decode(std::span<const std::uint8_t> datagram);

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
    [[nodiscard]] ExportGpu gpu() const noexcept;
    [[nodiscard]] ExportedLayout layout() const noexcept;
    [[nodiscard]] std::uint64_t copies() const noexcept;
    [[nodiscard]] std::uint32_t guest_refresh_microhz() const noexcept;
    [[nodiscard]] std::uint32_t reason() const noexcept;
};

[[nodiscard]] const char* to_string(ExportAnswer::Status status) noexcept;

}
