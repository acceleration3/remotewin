#include "host/mirror/state_sync.hpp"

#include <utility>

namespace rwin::host {

StateSync::StateSync(Ask ask_guest, Ask ask_compositor)
    : ask_guest_(std::move(ask_guest)), ask_compositor_(std::move(ask_compositor)) {}

bool StateSync::guest_expected() const noexcept { return guest_.asked.value_or(guest_.seen.value_or(false)); }

bool StateSync::compositor_expected() const noexcept {
    return compositor_.asked.value_or(compositor_.seen.value_or(false));
}

bool StateSync::answers_ask(Side& side, bool state, std::int64_t at) {
    if (!side.asked.has_value()) return false;
    if (*side.asked == state) {
        side.asked.reset();
        return true;
    }
    if (at - side.asked_at < kTransitNs) return true;
    side.asked.reset();
    return false;
}

void StateSync::ask(Side& side, bool state, std::int64_t at, const Ask& asking) {
    side.asked = state;
    side.asked_at = at;
    asking(state);
}

void StateSync::guest_says(bool state, std::int64_t at) {
    const bool changed = guest_.seen != state;
    guest_.seen = state;
    if (answers_ask(guest_, state, at) || !changed) return;
    if (compositor_expected() != state) ask(compositor_, state, at, ask_compositor_);
}

void StateSync::compositor_says(bool state, std::int64_t at) {
    const bool changed = compositor_.seen != state;
    compositor_.seen = state;
    if (answers_ask(compositor_, state, at) || !changed) return;
    if (guest_expected() != state) ask(guest_, state, at, ask_guest_);
}

void StateSync::want_guest(bool state, std::int64_t at) {
    if (guest_expected() != state) ask(guest_, state, at, ask_guest_);
}

void StateSync::compositor_anew(std::int64_t at) {
    compositor_ = Side{};
    if (guest_.seen.value_or(false)) ask(compositor_, true, at, ask_compositor_);
}

}
