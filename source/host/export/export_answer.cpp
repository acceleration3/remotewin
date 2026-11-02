#include "host/export/export_answer.hpp"

namespace rwin::host {

namespace {

constexpr std::size_t kBodyAt = 32;
constexpr std::uint8_t kOnPci = 1;

template <typename Number>
Number get(std::span<const std::uint8_t> bytes, std::size_t at) noexcept {
    std::uint64_t value = 0;
    for (std::size_t byte = 0; byte < sizeof(Number); ++byte) {
        value |= static_cast<std::uint64_t>(bytes[at + byte]) << (8 * byte);
    }
    return static_cast<Number>(value);
}

}

std::optional<ExportAnswer> ExportAnswer::decode(std::span<const std::uint8_t> datagram) {
    if (datagram.size() != kBytes || get<std::uint32_t>(datagram, 0) != kMagic
        || get<std::uint16_t>(datagram, 4) != kMajor) {
        return std::nullopt;
    }
    const auto status = get<std::uint32_t>(datagram, 12);
    if (status > static_cast<std::uint32_t>(Status::Unavailable)) return std::nullopt;
    ExportAnswer answer;
    answer.minor = get<std::uint16_t>(datagram, 6);
    answer.operation = get<std::uint32_t>(datagram, 8);
    answer.status = static_cast<Status>(status);
    answer.surface = get<std::uint64_t>(datagram, 16);
    answer.tag = get<std::uint64_t>(datagram, 24);
    for (std::size_t byte = 0; byte < answer.body.size(); ++byte) answer.body[byte] = datagram[kBodyAt + byte];
    return answer;
}

ExportGpu ExportAnswer::gpu() const noexcept {
    ExportGpu gpu;
    for (std::size_t byte = 0; byte < gpu.uuid.size(); ++byte) gpu.uuid[byte] = body[byte];
    if ((body[23] & kOnPci) != 0) {
        gpu.pci = PciAddress{get<std::uint32_t>(body, 16), body[20], body[21], body[22]};
    }
    return gpu;
}

ExportedLayout ExportAnswer::layout() const noexcept {
    return ExportedLayout{get<std::uint32_t>(body, 0),  get<std::uint32_t>(body, 4),  get<std::uint32_t>(body, 8),
                          get<std::uint32_t>(body, 12), get<std::uint32_t>(body, 16), get<std::uint64_t>(body, 24)};
}

std::uint64_t ExportAnswer::copies() const noexcept { return get<std::uint64_t>(body, 0); }
std::uint32_t ExportAnswer::guest_refresh_microhz() const noexcept { return get<std::uint32_t>(body, 8); }

std::uint32_t ExportAnswer::reason() const noexcept { return get<std::uint32_t>(body, 0); }

const char* to_string(ExportAnswer::Status status) noexcept {
    switch (status) {
        case ExportAnswer::Status::Ok:
            return "ok";
        case ExportAnswer::Status::Malformed:
            return "malformed";
        case ExportAnswer::Status::NotSupported:
            return "not supported";
        case ExportAnswer::Status::NotFound:
            return "not found";
        case ExportAnswer::Status::NoSpace:
            return "no space";
        case ExportAnswer::Status::Unavailable:
            return "unavailable";
    }
    return "unknown";
}

}
