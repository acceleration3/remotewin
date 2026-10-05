#pragma once

#include <cstdint>

namespace rwin::probe {

// The process the backdrop window lives in: this executable again, asked
// to be a backdrop. Ends when this object does - and on its own if this
// process ends first, because it watches.
class BackdropProcess {
public:
    // ``rgb`` is 0x00RRGGBB. Returns once the backdrop is on screen, or
    // after giving it a few seconds to be.
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

}  // namespace rwin::probe
