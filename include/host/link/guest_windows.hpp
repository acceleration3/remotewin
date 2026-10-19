#pragma once

#include "host/link/guest_window.hpp"
#include "proto/window_id.hpp"
#include "proto/z_order.hpp"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace rwin {
struct MinMax;
struct WindowFields;
}  // namespace rwin

namespace rwin::host {

// Every window the guest has told of and not yet said is gone, by its id,
// with the guest's stacking of them (RAIL's window and desktop orders, as
// a client keeps them).
//
// A resync (SyncBegin ... SyncEnd) says every window afresh: one known
// before that is not said again inside it is gone. A change to a window
// not known is not about any window here - one already gone, or named
// before its WindowNew - and changes nothing.
class GuestWindows {
public:
    // A window told of whole. For one known already, the fields in which it
    // now differs; for a new one, WindowFields::kAll.
    struct Told {
        bool known = false;
        std::uint32_t changed = 0;
    };
    Told tell(WindowId id, WindowFields fields);
    // ``fields`` read over the window's own (WindowFields::read); false for
    // a window not known.
    bool change(WindowId id, WindowFields fields);
    // False for a window not known.
    bool forget(WindowId id);
    bool limit(const MinMax& limits);
    void restack(ZOrder order);

    void begin_sync();
    // The windows the resync did not name, now forgotten.
    [[nodiscard]] std::vector<WindowId> end_sync();

    [[nodiscard]] const GuestWindow* find(WindowId id) const;
    // The window with that HWND, whatever its serial: for whoever knows a
    // window only by its handle, as the conformance probe does.
    [[nodiscard]] const GuestWindow* find_hwnd(std::uint32_t hwnd) const;
    [[nodiscard]] const ZOrder& order() const noexcept { return order_; }
    // Every window: in the guest's stacking order, topmost first, and those
    // it has not stacked yet after them.
    [[nodiscard]] std::vector<const GuestWindow*> stacked() const;
    [[nodiscard]] std::size_t size() const noexcept { return windows_.size(); }

private:
    std::unordered_map<WindowId, GuestWindow> windows_;
    // During a resync, the windows it has not named yet.
    std::unordered_set<WindowId> unnamed_;
    bool syncing_ = false;
    ZOrder order_;
};

}  // namespace rwin::host
