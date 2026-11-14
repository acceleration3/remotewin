#include "host/mirror/guest_application.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace rwin::host {

namespace {
constexpr std::string_view kNamespace = "lsw-guest.";
constexpr std::string_view kUnknown = "unknown";
constexpr std::string_view kUnnamed = "app";
constexpr std::string_view kExecutable = ".exe";
constexpr std::size_t kMostReadable = 40;
constexpr std::uint64_t kFnvBasis = 0xcbf29ce484222325ull;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ull;

bool letter(char c) { return c >= 'a' && c <= 'z'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

std::uint64_t hash_of(std::string_view text) {
    std::uint64_t hash = kFnvBasis;
    for (const char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= kFnvPrime;
    }
    return hash;
}

}

GuestApplication::GuestApplication(const WindowFields::AppId& app)
    : identity_(folded(app.aumid.empty() ? app.image : app.aumid)) {}

std::string GuestApplication::folded(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](char c) {
        if (c == '/') return '\\';
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
    });
    return text;
}

std::string GuestApplication::readable() const {
    std::string_view named = identity_;
    if (const std::size_t folder = named.find_last_of('\\'); folder != std::string_view::npos) {
        named.remove_prefix(folder + 1);
        if (named.ends_with(kExecutable)) named.remove_suffix(kExecutable.size());
    } else if (const std::size_t entry = named.find('!'); entry != std::string_view::npos) {
        named = named.substr(0, entry);
        named = named.substr(0, named.find('_'));
    }
    std::string text;
    for (const char c : named.substr(0, kMostReadable)) {
        if (letter(c) || digit(c) || c == '_') {
            text.push_back(c);
        } else if (!text.empty() && text.back() != '-') {
            text.push_back('-');
        }
    }
    while (!text.empty() && text.back() == '-') text.pop_back();
    if (text.empty()) return std::string(kUnnamed);
    if (!letter(text.front())) text.insert(text.begin(), '_');
    return text;
}

std::string GuestApplication::host_app_id() const {
    if (!known()) return std::string(kNamespace) + std::string(kUnknown);
    return std::format("{}{}-{:016x}", kNamespace, readable(), hash_of(identity_));
}

}
