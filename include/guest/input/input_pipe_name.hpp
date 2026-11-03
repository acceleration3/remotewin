#pragma once

#include <cstdint>
#include <string>

namespace rwin::guest {
class InputPipeName {
public:
    explicit InputPipeName(std::uint32_t session);

    [[nodiscard]] const wchar_t* c_str() const noexcept { return text_.c_str(); }

private:
    std::wstring text_;
};

}
