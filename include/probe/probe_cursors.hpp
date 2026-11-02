#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct HICON__;

namespace rwin::probe {
class ProbeCursors {
public:
    static constexpr std::uint32_t kAnimatedSteps = 4;
    static constexpr std::uint32_t kAnimatedSide = 32;
    static constexpr std::uint32_t kLargeSide = 96;

    ProbeCursors();
    ~ProbeCursors();

    ProbeCursors(const ProbeCursors&) = delete;
    ProbeCursors& operator=(const ProbeCursors&) = delete;
    ProbeCursors(ProbeCursors&&) = delete;
    ProbeCursors& operator=(ProbeCursors&&) = delete;

    [[nodiscard]] std::optional<HICON__*> cursor_for(std::uint32_t code, std::string& failure) const;

private:
    [[nodiscard]] static HICON__* make_colour(std::uint32_t side, std::uint32_t hotspot);
    [[nodiscard]] static HICON__* make_monochrome();
    [[nodiscard]] static HICON__* load_animated(std::string& failure);

    std::string animated_failure_;
    HICON__* colour_ = nullptr;
    HICON__* monochrome_ = nullptr;
    HICON__* animated_ = nullptr;
    HICON__* large_ = nullptr;
};

}
