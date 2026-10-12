#pragma once

#include <QObject>

#include <cstdint>
#include <memory>
#include <vector>

struct wl_output;
struct wl_surface;
struct wp_presentation;
struct wp_presentation_feedback;

namespace rwin::host {

// When the compositor put a surface's commits on screen (wp_presentation),
// on CLOCK_MONOTONIC: the moment the picture turned to light, and the
// output's refresh period then.
//
// Bound by this client itself on Qt's connection, as PointerGlobals binds
// its globals; heard on Qt's own queue. Asks nothing where the compositor
// has no presentation times, or keeps them on another clock.
class PresentationTimes : public QObject {
    Q_OBJECT
public:
    explicit PresentationTimes(QObject* parent = nullptr);
    ~PresentationTimes() override;

    PresentationTimes(const PresentationTimes&) = delete;
    PresentationTimes& operator=(const PresentationTimes&) = delete;
    PresentationTimes(PresentationTimes&&) = delete;
    PresentationTimes& operator=(PresentationTimes&&) = delete;

    [[nodiscard]] bool offered() const noexcept;

    // The next commit of ``surface`` is ``frame``: presented() or
    // discarded() says what became of it. Nothing where not offered().
    void ask(wl_surface* surface, std::uint64_t frame);

Q_SIGNALS:
    // ``refresh`` is 0 where the output has no fixed one.
    void presented(quint64 frame, qint64 when, qint64 refresh);
    // Replaced by a later commit before it was shown, or never shown.
    void discarded(quint64 frame);

private:
    struct Bound;
    struct Asked {
        wp_presentation_feedback* feedback;
        std::uint64_t frame;
    };

    static void on_clock(void* self, wp_presentation* presentation, std::uint32_t clock);
    static void on_sync_output(void* self, wp_presentation_feedback* feedback, wl_output* output);
    static void on_presented(void* self, wp_presentation_feedback* feedback, std::uint32_t seconds_high,
                             std::uint32_t seconds_low, std::uint32_t nanoseconds, std::uint32_t refresh,
                             std::uint32_t sequence_high, std::uint32_t sequence_low, std::uint32_t flags);
    static void on_discarded(void* self, wp_presentation_feedback* feedback);
    // The frame ``feedback`` was asked for, its feedback let go of.
    [[nodiscard]] std::uint64_t answered(wp_presentation_feedback* feedback);

    std::unique_ptr<Bound> bound_;
    std::vector<Asked> asked_;
};

}  // namespace rwin::host
