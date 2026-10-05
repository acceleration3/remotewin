#include "host/export/export_request.hpp"

namespace rwin::host {

namespace {

template <typename Number>
void put(std::array<std::uint8_t, ExportRequest::kBytes>& bytes, std::size_t at, Number value) {
    for (std::size_t byte = 0; byte < sizeof(Number); ++byte) {
        bytes[at + byte] = static_cast<std::uint8_t>(static_cast<std::uint64_t>(value) >> (8 * byte));
    }
}

}  // namespace

std::array<std::uint8_t, ExportRequest::kBytes> ExportRequest::encode() const noexcept {
    std::array<std::uint8_t, kBytes> bytes{};
    put(bytes, 0, kMagic);
    put(bytes, 4, kMajor);
    put(bytes, 6, kMinor);
    put(bytes, 8, static_cast<std::uint32_t>(operation));
    put(bytes, 16, surface);
    put(bytes, 24, tag);
    return bytes;
}

}  // namespace rwin::host
