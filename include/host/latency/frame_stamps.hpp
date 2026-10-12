#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwin::host {

// This side's monotonic clock in nanoseconds: CLOCK_MONOTONIC, the clock the
// compositor's presentation times and the GPU device's are on.
[[nodiscard]] std::int64_t now_ns() noexcept;

// When one guest frame passed each stage on its way to the screen, on this
// side's clock (now_ns); 0 where it was not seen. The stages are in the
// order a frame passes them.
struct FrameStamps {
    enum class Stage : std::size_t {
        // The guest's blank its compositor composed it at.
        Composed,
        // The guest took it from the capture.
        Taken,
        // The guest asked its GPU to copy it into a slot.
        Copied,
        // The guest named it to this side (FrameInSlot).
        Received,
        // The GPU device ran the guest's copy.
        Executed,
        // The GPU device handed over the fence for the copy.
        Ready,
        // A mirror took it to draw.
        Shown,
        // Drawn into the mirror.
        Drawn,
        // The mirror's buffer went to the compositor.
        Swapped,
        // The compositor put it on screen (wp_presentation).
        Presented,
    };
    static constexpr std::size_t kStages = static_cast<std::size_t>(Stage::Presented) + 1;

    std::array<std::int64_t, kStages> at{};
    // The output's refresh period as the compositor said with Presented;
    // 0 when it did not say.
    std::int64_t refresh_ns = 0;

    void note(Stage stage, std::int64_t when) noexcept { at[static_cast<std::size_t>(stage)] = when; }
    void note_now(Stage stage) noexcept { note(stage, now_ns()); }
    [[nodiscard]] std::int64_t of(Stage stage) const noexcept { return at[static_cast<std::size_t>(stage)]; }
};

[[nodiscard]] const char* to_string(FrameStamps::Stage stage) noexcept;

}  // namespace rwin::host
