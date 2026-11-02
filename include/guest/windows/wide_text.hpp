#pragma once

#include <string>
#include <string_view>

namespace rwin::guest {
[[nodiscard]] std::string utf8_of(std::wstring_view wide);

}
