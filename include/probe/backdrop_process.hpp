#pragma once

#include <cstdint>

namespace rwin::probe {
class BackdropProcess {
public:
    explicit BackdropProcess(std::uint32_t rgb);
    ~BackdropProcess();

    BackdropProcess(const BackdropProcess&) = delete;
    BackdropProcess& operator=(const BackdropProcess&) = delete;
    BackdropProcess(BackdropProcess&&) = delete;
    BackdropProcess& operator=(BackdropProcess&&) = delete;

    [[nodiscard]] bool running() const noexcept { return process_ != nullptr; }

private:
    [[nodiscard]] bool wait_until_shown() const;

    void* process_ = nullptr;
    std::uint32_t process_id_ = 0;
};

}
