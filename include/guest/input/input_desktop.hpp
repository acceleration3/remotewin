#pragma once

#include <string>

namespace rwin::guest {
class InputDesktop {
public:
    InputDesktop() = default;
    ~InputDesktop();

    InputDesktop(const InputDesktop&) = delete;
    InputDesktop& operator=(const InputDesktop&) = delete;

    bool follow();

private:
    void* attached_ = nullptr;
    std::wstring name_;
};

}
