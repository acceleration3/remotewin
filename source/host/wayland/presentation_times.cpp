#include "host/wayland/presentation_times.hpp"

#include "host/wayland/owned_proxy.hpp"
#include "logging/logger.hpp"

#include <QGuiApplication>

#include <presentation-time-client-protocol.h>
#include <wayland-client.h>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace rwin::host {

namespace {

constexpr std::uint32_t kPresentationVersion = 1;
constexpr std::int64_t kNanosecondsPerSecond = 1'000'000'000;
// An answer never comes for a surface that is never committed again; past
// this many waiting, the oldest is given up on rather than kept for good.
constexpr std::size_t kMostAsked = 64;

void destroy_queue(wl_event_queue* queue) { wl_event_queue_destroy(queue); }
void destroy_wrapper(wl_display* wrapper) { wl_proxy_wrapper_destroy(wrapper); }

void bind(void* data, wl_registry* registry, std::uint32_t name, const char* interface, std::uint32_t version) {
    if (std::strcmp(interface, wp_presentation_interface.name) != 0) return;
    *static_cast<wp_presentation**>(data) = static_cast<wp_presentation*>(
        wl_registry_bind(registry, name, &wp_presentation_interface, std::min(version, kPresentationVersion)));
}

void unbind(void* /*data*/, wl_registry* /*registry*/, std::uint32_t /*name*/) {}

}  // namespace

struct PresentationTimes::Bound {
    OwnedProxy<wp_presentation, &wp_presentation_destroy> presentation;
    bool monotonic = false;
};

// The global and its clock are learnt on a queue of this object's own, so
// the round trips dispatch nothing of Qt's; the global is then moved to
// Qt's default queue, where the feedback made from it is heard.
PresentationTimes::PresentationTimes(QObject* parent) : QObject(parent), bound_(std::make_unique<Bound>()) {
    auto* native = qGuiApp != nullptr ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (native == nullptr || native->display() == nullptr) return;
    wl_display* display = native->display();
    const OwnedProxy<wl_event_queue, &destroy_queue> queue{wl_display_create_queue(display)};
    const OwnedProxy<wl_display, &destroy_wrapper> wrapper{static_cast<wl_display*>(wl_proxy_create_wrapper(display))};
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(wrapper.get()), queue.get());
    const OwnedProxy<wl_registry, &wl_registry_destroy> registry{wl_display_get_registry(wrapper.get())};
    wp_presentation* presentation = nullptr;
    static constexpr wl_registry_listener kRegistry{&bind, &unbind};
    wl_registry_add_listener(registry.get(), &kRegistry, &presentation);
    if (wl_display_roundtrip_queue(display, queue.get()) < 0 || presentation == nullptr) {
        RWIN_INFO("present", "the compositor gives no presentation times");
        return;
    }
    bound_->presentation.reset(presentation);
    static constexpr wp_presentation_listener kPresentation{&PresentationTimes::on_clock};
    wp_presentation_add_listener(presentation, &kPresentation, this);
    if (wl_display_roundtrip_queue(display, queue.get()) < 0 || !bound_->monotonic) {
        RWIN_INFO("present", "the compositor's presentation times are not on CLOCK_MONOTONIC; not asked for");
        bound_->presentation.reset();
        return;
    }
    wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(presentation), nullptr);
}

PresentationTimes::~PresentationTimes() {
    for (const Asked& asked : asked_) wp_presentation_feedback_destroy(asked.feedback);
}

bool PresentationTimes::offered() const noexcept { return bound_->presentation != nullptr; }

void PresentationTimes::ask(wl_surface* surface, std::uint64_t frame) {
    if (!offered() || surface == nullptr) return;
    if (asked_.size() == kMostAsked) {
        wp_presentation_feedback_destroy(asked_.front().feedback);
        asked_.erase(asked_.begin());
    }
    struct wp_presentation_feedback* feedback = ::wp_presentation_feedback(bound_->presentation.get(), surface);
    static constexpr wp_presentation_feedback_listener kFeedback{&PresentationTimes::on_sync_output,
                                                                 &PresentationTimes::on_presented,
                                                                 &PresentationTimes::on_discarded};
    wp_presentation_feedback_add_listener(feedback, &kFeedback, this);
    asked_.push_back(Asked{feedback, frame});
}

void PresentationTimes::on_clock(void* self, wp_presentation* /*presentation*/, std::uint32_t clock) {
    static_cast<PresentationTimes*>(self)->bound_->monotonic = clock == CLOCK_MONOTONIC;
}

void PresentationTimes::on_sync_output(void* /*self*/, struct wp_presentation_feedback* /*feedback*/,
                                       wl_output* /*output*/) {}

void PresentationTimes::on_presented(void* self, struct wp_presentation_feedback* feedback, std::uint32_t seconds_high,
                                     std::uint32_t seconds_low, std::uint32_t nanoseconds, std::uint32_t refresh,
                                     std::uint32_t /*sequence_high*/, std::uint32_t /*sequence_low*/,
                                     std::uint32_t /*flags*/) {
    auto* times = static_cast<PresentationTimes*>(self);
    const std::uint64_t frame = times->answered(feedback);
    const auto seconds = static_cast<std::int64_t>((static_cast<std::uint64_t>(seconds_high) << 32) | seconds_low);
    Q_EMIT times->presented(frame, seconds * kNanosecondsPerSecond + nanoseconds, refresh);
}

void PresentationTimes::on_discarded(void* self, struct wp_presentation_feedback* feedback) {
    auto* times = static_cast<PresentationTimes*>(self);
    Q_EMIT times->discarded(times->answered(feedback));
}

std::uint64_t PresentationTimes::answered(struct wp_presentation_feedback* feedback) {
    std::uint64_t frame = 0;
    const auto found = std::find_if(asked_.begin(), asked_.end(),
                                    [&](const Asked& asked) { return asked.feedback == feedback; });
    if (found != asked_.end()) {
        frame = found->frame;
        asked_.erase(found);
    }
    wp_presentation_feedback_destroy(feedback);
    return frame;
}

}  // namespace rwin::host
