#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rwin::host {
[[nodiscard]] std::int64_t now_ns() noexcept;

struct FrameStamps {
    enum class Stage : std::size_t {
        Composed,
        Taken,
        Copied,
        Received,
        Executed,
        Ready,
        Shown,
        Acquired,
        Committed,
        Presented,
    };
    static constexpr std::size_t kStages = static_cast<std::size_t>(Stage::Presented) + 1;

    std::array<std::int64_t, kStages> at{};
    std::int64_t refresh_ns = 0;

    void note(Stage stage, std::int64_t when) noexcept { at[static_cast<std::size_t>(stage)] = when; }
    void note_now(Stage stage) noexcept { note(stage, now_ns()); }
    [[nodiscard]] std::int64_t of(Stage stage) const noexcept { return at[static_cast<std::size_t>(stage)]; }
};

[[nodiscard]] const char* to_string(FrameStamps::Stage stage) noexcept;

}
