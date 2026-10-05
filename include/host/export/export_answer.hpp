#pragma once

#include "host/export/export_gpu.hpp"
#include "host/export/exported_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace rwin::host {

// One answer, or the one event, from the GPU device's export socket
// (accel-virt's docs/external-export.md 3.2): 64 bytes, little-endian - the
// operation answered, a status, the surface and the tag, then a 32-byte
// body the operation decides.
struct ExportAnswer {
    static constexpr std::size_t kBytes = 64;
    static constexpr std::uint32_t kMagic = 0x41585641;  // "AVXA"
    static constexpr std::uint16_t kMajor = 1;

    // The request's operation (ExportRequest::Operation), or one of these.
    static constexpr std::uint32_t kUnreadable = 0;
    // Unasked: the surface is gone (guest released it), or every surface,
    // as 0, at a reset of the device.
    static constexpr std::uint32_t kRevoked = 0x80;

    enum class Status : std::uint32_t {
        Ok = 0,
        Malformed = 1,
        NotSupported = 2,
        NotFound = 3,
        NoSpace = 4,
        Unavailable = 5,
    };

    std::uint32_t operation = kUnreadable;
    Status status = Status::Malformed;
    std::uint64_t surface = 0;
    std::uint64_t tag = 0;
    std::array<std::uint8_t, 32> body{};

    // Nothing for a datagram that is not an answer of major version 1: of
    // another length, magic or major, or with a status the version lacks.
    [[nodiscard]] static std::optional<ExportAnswer> decode(std::span<const std::uint8_t> datagram);

    [[nodiscard]] bool ok() const noexcept { return status == Status::Ok; }
    // A hello's body.
    [[nodiscard]] ExportGpu gpu() const noexcept;
    // An export's body.
    [[nodiscard]] ExportedLayout layout() const noexcept;
    // A sync's body: the copies into the surface the host has run.
    [[nodiscard]] std::uint64_t copies() const noexcept;
    // A revocation's body: 1 the guest released the surface, 2 a reset.
    [[nodiscard]] std::uint32_t reason() const noexcept;
};

[[nodiscard]] const char* to_string(ExportAnswer::Status status) noexcept;

}  // namespace rwin::host
