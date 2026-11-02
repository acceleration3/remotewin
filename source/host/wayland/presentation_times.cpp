#include "host/wayland/presentation_times.hpp"

#include <presentation-time-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>

namespace rwin::host {

namespace {

constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr std::size_t kMostAsked = 16;

}

PresentationTimes::PresentationTimes(wp_presentation* presentation,
                                     std::function<void(const FrameStamps&)> presented)
    : presentation_(presentation), presented_(std::move(presented)) {}

PresentationTimes::~PresentationTimes() {
    for (const Asked& asked : asked_) wp_presentation_feedback_destroy(asked.feedback);
}

void PresentationTimes::ask(wl_surface* surface, const FrameStamps& stamps) {
    if (presentation_ == nullptr) return;
    if (asked_.size() == kMostAsked) {
        wp_presentation_feedback_destroy(asked_.front().feedback);
        asked_.erase(asked_.begin());
    }
    struct wp_presentation_feedback* feedback = ::wp_presentation_feedback(presentation_, surface);
    static constexpr wp_presentation_feedback_listener kFeedback{&PresentationTimes::on_sync_output,
                                                                 &PresentationTimes::on_presented,
                                                                 &PresentationTimes::on_discarded};
    wp_presentation_feedback_add_listener(feedback, &kFeedback, this);
    asked_.push_back(Asked{feedback, stamps});
}

void PresentationTimes::on_sync_output(void* , struct wp_presentation_feedback* ,
                                       wl_output* ) {}

void PresentationTimes::on_presented(void* self, struct wp_presentation_feedback* feedback, std::uint32_t seconds_high,
                                     std::uint32_t seconds_low, std::uint32_t nanoseconds, std::uint32_t refresh,
                                     std::uint32_t , std::uint32_t ,
                                     std::uint32_t ) {
    auto* times = static_cast<PresentationTimes*>(self);
    FrameStamps stamps = times->answered(feedback);
    const auto seconds = static_cast<std::int64_t>((static_cast<std::uint64_t>(seconds_high) << 32) | seconds_low);
    stamps.note(FrameStamps::Stage::Presented, seconds * kNanosecondsPerSecond + nanoseconds);
    stamps.refresh_ns = refresh;
    times->presented_(stamps);
}

void PresentationTimes::on_discarded(void* self, struct wp_presentation_feedback* feedback) {
    static_cast<void>(static_cast<PresentationTimes*>(self)->answered(feedback));
}

FrameStamps PresentationTimes::answered(struct wp_presentation_feedback* feedback) {
    FrameStamps stamps;
    const auto found = std::find_if(asked_.begin(), asked_.end(),
                                    [&](const Asked& asked) { return asked.feedback == feedback; });
    if (found != asked_.end()) {
        stamps = found->stamps;
        asked_.erase(found);
    }
    wp_presentation_feedback_destroy(feedback);
    return stamps;
}

}
