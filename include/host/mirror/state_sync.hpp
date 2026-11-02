#pragma once

#include <cstdint>
#include <functional>
#include <optional>

namespace rwin::host {
class StateSync {
public:
    using Ask = std::function<void(bool)>;

    StateSync(Ask ask_guest, Ask ask_compositor);

    void guest_says(bool state, std::int64_t at);
    void compositor_says(bool state, std::int64_t at);
    void want_guest(bool state, std::int64_t at);

    [[nodiscard]] bool guest_expected() const noexcept;
    [[nodiscard]] bool compositor_expected() const noexcept;
    [[nodiscard]] bool guest_heading() const noexcept { return guest_.asked.has_value(); }

    void compositor_anew(std::int64_t at);

private:
    struct Side {
        std::optional<bool> seen;
        std::optional<bool> asked;
        std::int64_t asked_at = 0;
    };

    static constexpr std::int64_t kTransitNs = 1'000'000'000;

    static bool answers_ask(Side& side, bool state, std::int64_t at);
    static void ask(Side& side, bool state, std::int64_t at, const Ask& asking);

    Ask ask_guest_;
    Ask ask_compositor_;
    Side guest_;
    Side compositor_;
};

}
