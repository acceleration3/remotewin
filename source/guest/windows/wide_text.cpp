#include "guest/windows/wide_text.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

std::string utf8_of(std::wstring_view wide) {
    if (wide.empty()) return {};
    const int length = static_cast<int>(wide.size());
    const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), length, out.data(), bytes, nullptr, nullptr);
    return out;
}

}
