#pragma once

#include <deque>
#include <functional>
#include <utility>

namespace rwin::host {
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

}
