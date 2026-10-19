#pragma once

#include <string>
#include <string_view>

namespace rwin::guest {

// Windows' UTF-16 as the wire's UTF-8.
[[nodiscard]] std::string utf8_of(std::wstring_view wide);

}  // namespace rwin::guest
