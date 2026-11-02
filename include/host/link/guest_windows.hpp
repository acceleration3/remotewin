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
}

namespace rwin::host {
class GuestWindows {
public:
    struct Told {
        bool known = false;
        std::uint32_t changed = 0;
    };
    Told tell(WindowId id, WindowFields fields);
    bool change(WindowId id, WindowFields fields);
    bool forget(WindowId id);
    bool limit(const MinMax& limits);
    void restack(ZOrder order);

    void begin_sync();
    [[nodiscard]] std::vector<WindowId> end_sync();

    [[nodiscard]] const GuestWindow* find(WindowId id) const;
    [[nodiscard]] const GuestWindow* find_hwnd(std::uint32_t hwnd) const;
    [[nodiscard]] const ZOrder& order() const noexcept { return order_; }
    [[nodiscard]] std::vector<const GuestWindow*> stacked() const;
    [[nodiscard]] std::size_t size() const noexcept { return windows_.size(); }

private:
    std::unordered_map<WindowId, GuestWindow> windows_;
    std::unordered_set<WindowId> unnamed_;
    bool syncing_ = false;
    ZOrder order_;
};

}
