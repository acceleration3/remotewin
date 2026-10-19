#pragma once

#include "proto/window_id.hpp"

#include <unordered_map>

namespace rwin::guest {

class Outbox;

// The icons of the windows the taskbar gives a button (RAIL's Window Icon
// orders): the one Windows shows for each, sent when it is first seen and
// again whenever the window's icon handle changes - WM_SETICON raises no
// event, so the tracker's sweep looks.
//
// Lives on the window thread (WindowWatch).
class WindowIcons {
public:
    explicit WindowIcons(Outbox& outbox) noexcept : outbox_(outbox) {}

    void look(void* window, WindowId id);
    void forget(WindowId id);

private:
    Outbox& outbox_;
    // The icon handle last sent, by WindowId::key.
    std::unordered_map<std::uint64_t, void*> sent_;
};

}  // namespace rwin::guest
