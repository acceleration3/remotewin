#pragma once

#include "proto/window_id.hpp"

#include <unordered_map>

namespace rwin::guest {

class Outbox;

class WindowIcons {
public:
    explicit WindowIcons(Outbox& outbox) noexcept : outbox_(outbox) {}

    void look(void* window, WindowId id);
    void forget(WindowId id);

private:
    Outbox& outbox_;
    std::unordered_map<std::uint64_t, void*> sent_;
};

}
