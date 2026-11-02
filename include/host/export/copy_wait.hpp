#pragma once

#include <cstdint>

namespace rwin::host {
class CopyWait {
public:
    enum class Verdict {
        Ready,
        AskAgain,
        GiveUp,
    };

    static constexpr int kRetryMs = 1;
    static constexpr int kMostAsks = 2000;

    explicit CopyWait(std::uint64_t copies) noexcept : copies_(copies) {}

    [[nodiscard]] std::uint64_t copies() const noexcept { return copies_; }

    [[nodiscard]] Verdict answered(std::uint64_t copies_run) noexcept;

private:
    std::uint64_t copies_;
    int asks_ = 0;
};

}
