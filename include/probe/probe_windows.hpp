#pragma once

#include "probe/probe_window.hpp"
#include "proto/probe/window_truth.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct HWND__;

namespace rwin::probe {
class ProbeWindows {
public:
    void adopt(std::unique_ptr<ProbeWindow> window);
    void follow(std::uint64_t handle);

    [[nodiscard]] bool has(std::uint64_t handle) const;
    [[nodiscard]] ProbeWindow* made(std::uint64_t handle) const;

    [[nodiscard]] std::optional<WindowTruth> truth(std::uint64_t handle) const;

    [[nodiscard]] std::uint64_t new_dialog() const;

    [[nodiscard]] bool destroy(std::uint64_t handle);
    void destroy_all();

private:
    [[nodiscard]] std::int32_t z_rank(HWND__* window) const;

    std::vector<std::unique_ptr<ProbeWindow>> made_;
    std::vector<std::uint64_t> handles_;
};

}
