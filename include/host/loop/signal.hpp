#pragma once

#include <deque>
#include <functional>
#include <utility>

namespace rwin::host {

// Something that happened, told to everyone who asked to hear of it, in
// the order they asked. Whoever connects outlives the emitter or is torn
// down with it: there is no disconnecting.
//
// A deque: a slot that connects another while it is being called is not
// moved under itself.
template <typename... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    void connect(Slot slot) { slots_.push_back(std::move(slot)); }

    void operator()(Args... args) const {
        for (std::size_t at = 0; at < slots_.size(); ++at) slots_[at](args...);
    }

private:
    std::deque<Slot> slots_;
};

}  // namespace rwin::host
