#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace rwin::host {

struct FrameStamps;

class CadenceMeter {
public:
    void take(const FrameStamps& frame) noexcept;

    [[nodiscard]] std::size_t steady() const noexcept { return steady_; }
    [[nodiscard]] std::size_t held() const noexcept { return held_; }
    [[nodiscard]] std::size_t rushed() const noexcept { return rushed_; }
    [[nodiscard]] std::size_t unshown_blanks() const noexcept { return unshown_blanks_; }
    [[nodiscard]] std::int64_t longest_on_screen_ns() const noexcept { return longest_ns_; }

    [[nodiscard]] std::string summary() const;
    void restart() noexcept;

private:
    std::int64_t composed_ = 0;
    std::int64_t presented_ = 0;
    std::size_t steady_ = 0;
    std::size_t held_ = 0;
    std::size_t rushed_ = 0;
    std::size_t unshown_blanks_ = 0;
    std::int64_t longest_ns_ = 0;
};

}
