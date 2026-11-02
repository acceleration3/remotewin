#pragma once

#include <cstdint>

struct HWND__;

namespace rwin::probe {
class BackdropWindow {
public:
    explicit BackdropWindow(std::uint32_t rgb);
    ~BackdropWindow();

    BackdropWindow(const BackdropWindow&) = delete;
    BackdropWindow& operator=(const BackdropWindow&) = delete;
    BackdropWindow(BackdropWindow&&) = delete;
    BackdropWindow& operator=(BackdropWindow&&) = delete;

    [[nodiscard]] bool made() const noexcept { return window_ != nullptr; }

    void stay_while(std::uint32_t parent_process_id) const;

    static constexpr const wchar_t* kClassName = L"rwin-probe-backdrop";

private:
    HWND__* window_ = nullptr;
    void* brush_ = nullptr;
};

}
