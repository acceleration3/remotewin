#include "host/latency/cadence_meter.hpp"

#include "host/latency/frame_stamps.hpp"

#include <algorithm>
#include <format>

namespace rwin::host {

namespace {
constexpr std::int64_t kAtRest = 6;

std::int64_t refreshes(std::int64_t gap, std::int64_t refresh) { return (gap + refresh / 2) / refresh; }

}

void CadenceMeter::take(const FrameStamps& frame) noexcept {
    const std::int64_t composed = frame.of(FrameStamps::Stage::Composed);
    const std::int64_t presented = frame.of(FrameStamps::Stage::Presented);
    const std::int64_t refresh = frame.refresh_ns;
    if (composed == 0 || presented == 0 || refresh <= 0) return;
    const std::int64_t composed_before = composed_;
    const std::int64_t presented_before = presented_;
    composed_ = composed;
    presented_ = presented;
    if (composed_before == 0 || composed <= composed_before || presented < presented_before) return;
    const std::int64_t blanks = refreshes(composed - composed_before, refresh);
    const std::int64_t on_screen = refreshes(presented - presented_before, refresh);
    if (blanks > kAtRest || on_screen > kAtRest) return;
    longest_ns_ = std::max(longest_ns_, presented - presented_before);
    if (blanks > 1) unshown_blanks_ += static_cast<std::size_t>(blanks - 1);
    if (on_screen == blanks) {
        ++steady_;
    } else if (on_screen > blanks) {
        ++held_;
    } else {
        ++rushed_;
    }
}

std::string CadenceMeter::summary() const {
    return std::format("cadence {} steady, {} held, {} rushed; {} blanks without a frame; longest on screen {:.1f} ms",
                       steady_, held_, rushed_, unshown_blanks_, static_cast<double>(longest_ns_) / 1e6);
}

void CadenceMeter::restart() noexcept {
    steady_ = 0;
    held_ = 0;
    rushed_ = 0;
    unshown_blanks_ = 0;
    longest_ns_ = 0;
}

}
