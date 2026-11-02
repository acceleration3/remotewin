#include "host/export/pci_address.hpp"

#include <charconv>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include <sys/stat.h>
#include <sys/sysmacros.h>

namespace rwin::host {

namespace {

constexpr int kHex = 16;
constexpr std::uint8_t kMostDevices = 0x1F;
constexpr std::uint8_t kMostFunctions = 7;

template <typename Number>
bool take(std::string_view& text, std::size_t digits, char separator, Number& out) {
    if (text.size() < digits) return false;
    unsigned value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + digits, value, kHex);
    if (error != std::errc{} || end != text.data() + digits) return false;
    out = static_cast<Number>(value);
    text.remove_prefix(digits);
    if (separator == '\0') return text.empty();
    if (text.empty() || text.front() != separator) return false;
    text.remove_prefix(1);
    return true;
}

}

std::optional<PciAddress> PciAddress::parse(std::string_view text) {
    PciAddress address;
    if (!take(text, 4, ':', address.domain) || !take(text, 2, ':', address.bus)
        || !take(text, 2, '.', address.device) || !take(text, 1, '\0', address.function)) {
        return std::nullopt;
    }
    if (address.device > kMostDevices || address.function > kMostFunctions) return std::nullopt;
    return address;
}

std::optional<PciAddress> PciAddress::of_drm_node(const std::string& node) {
    struct stat described {};
    if (::stat(node.c_str(), &described) != 0 || !S_ISCHR(described.st_mode)) return std::nullopt;
    const std::string link = "/sys/dev/char/" + std::to_string(major(described.st_rdev)) + ":"
                             + std::to_string(minor(described.st_rdev)) + "/device";
    std::error_code failed;
    const std::filesystem::path device = std::filesystem::canonical(link, failed);
    if (failed) return std::nullopt;
    return parse(device.filename().string());
}

std::string PciAddress::to_string() const {
    char text[sizeof "0000:00:00.0"] = {};
    std::snprintf(text, sizeof text, "%04x:%02x:%02x.%x", static_cast<unsigned>(domain), static_cast<unsigned>(bus),
                  static_cast<unsigned>(device), static_cast<unsigned>(function));
    return text;
}

std::string PciAddress::prime_name() const {
    char text[sizeof "pci-0000_00_00_0"] = {};
    std::snprintf(text, sizeof text, "pci-%04x_%02x_%02x_%x", static_cast<unsigned>(domain),
                  static_cast<unsigned>(bus), static_cast<unsigned>(device), static_cast<unsigned>(function));
    return text;
}

}
