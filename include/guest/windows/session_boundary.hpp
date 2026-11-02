#pragma once

#include <string_view>

namespace rwin::guest {
class SessionBoundary {
public:
    [[nodiscard]] bool contains(std::wstring_view image_name) const;
};

}
