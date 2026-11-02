#pragma once

#include "host/latency/frame_stamps.hpp"

#include <cstdint>
#include <functional>
#include <vector>

struct wl_output;
struct wl_surface;
struct wp_presentation;
struct wp_presentation_feedback;

namespace rwin::host {
class PresentationTimes {
public:
    PresentationTimes(wp_presentation* presentation, std::function<void(const FrameStamps&)> presented);
    ~PresentationTimes();

    PresentationTimes(const PresentationTimes&) = delete;
    PresentationTimes& operator=(const PresentationTimes&) = delete;
    PresentationTimes(PresentationTimes&&) = delete;
    PresentationTimes& operator=(PresentationTimes&&) = delete;

    void ask(wl_surface* surface, const FrameStamps& stamps);

private:
    struct Asked {
        wp_presentation_feedback* feedback;
        FrameStamps stamps;
    };

    static void on_sync_output(void* self, wp_presentation_feedback* feedback, wl_output* output);
    static void on_presented(void* self, wp_presentation_feedback* feedback, std::uint32_t seconds_high,
                             std::uint32_t seconds_low, std::uint32_t nanoseconds, std::uint32_t refresh,
                             std::uint32_t sequence_high, std::uint32_t sequence_low, std::uint32_t flags);
    static void on_discarded(void* self, wp_presentation_feedback* feedback);
    [[nodiscard]] FrameStamps answered(wp_presentation_feedback* feedback);

    wp_presentation* presentation_;
    std::function<void(const FrameStamps&)> presented_;
    std::vector<Asked> asked_;
};

}
