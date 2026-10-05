#pragma once

#include "probe/probe_window.hpp"
#include "proto/probe/window_truth.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

struct HWND__;

namespace rwin::probe {

// Every window the probe has: the ones it made to a spec, which it owns,
// and the ones Windows made for it - dialogs, message boxes - which it
// only follows. Each is reported with where it stands among the rest.
class ProbeWindows {
public:
    void adopt(std::unique_ptr<ProbeWindow> window);
    // A window Windows made and ends itself.
    void follow(std::uint64_t handle);

    [[nodiscard]] bool has(std::uint64_t handle) const;
    // A window it made to a spec, not one Windows made for it.
    [[nodiscard]] ProbeWindow* made(std::uint64_t handle) const;

    // The truth of a window this has, gone or not; nothing for one it
    // never had.
    [[nodiscard]] std::optional<WindowTruth> truth(std::uint64_t handle) const;

    // A dialog of this thread's that is up and that this does not have
    // yet: the one a modal call just opened.
    [[nodiscard]] std::uint64_t new_dialog() const;

    // DestroyWindow, through the window's owner here when it has one.
    [[nodiscard]] bool destroy(std::uint64_t handle);
    // Newest first, so an owned window goes before its owner rather than
    // being taken down by it; the followed ones before any, since they
    // are owned by the made ones or by nobody.
    void destroy_all();

private:
    [[nodiscard]] std::int32_t z_rank(HWND__* window) const;

    std::vector<std::unique_ptr<ProbeWindow>> made_;
    // Everything, in the order it arrived.
    std::vector<std::uint64_t> handles_;
};

}  // namespace rwin::probe
