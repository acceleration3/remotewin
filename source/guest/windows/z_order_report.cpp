#include "guest/windows/z_order_report.hpp"

#include "guest/session/outbox.hpp"
#include "guest/windows/marker_window.hpp"
#include "guest/windows/window_reader.hpp"
#include "guest/windows/window_tracker.hpp"
#include "logging/logger.hpp"
#include "proto/message_writer.hpp"

// clang-format off
#include <windows.h>
#include <dwmapi.h>
// clang-format on

#include <vector>

namespace rwin::guest {

namespace {

BOOL CALLBACK collect(HWND window, LPARAM into) {
    reinterpret_cast<std::vector<HWND>*>(into)->push_back(window);
    return TRUE;
}

bool cloaked(HWND window) {
    DWORD state = 0;
    return SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &state, sizeof(state))) && state != 0;
}

// GetGUIThreadInfo's active window, taken to its root: what RAIL reports.
HWND active_window() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (::GetGUIThreadInfo(0, &gui) == FALSE || gui.hwndActive == nullptr) return nullptr;
    HWND root = ::GetAncestor(gui.hwndActive, GA_ROOT);
    return root != nullptr ? root : gui.hwndActive;
}

}  // namespace

void ZOrderReport::expect(void* window) noexcept {
    expected_ = window;
    wait_ms_ = 0;
    due_.reset();
}

ZOrderReport::Stacking ZOrderReport::current() const {
    Stacking stacking;
    std::vector<HWND> windows;
    ::EnumWindows(&collect, reinterpret_cast<LPARAM>(&windows));
    const auto marker = static_cast<HWND>(marker_.handle());
    for (HWND window : windows) {
        if (stacking.order.windows.size() >= ZOrder::kMaxWindows) break;
        std::optional<WindowId> id;
        if (window == marker) {
            id = kMonitorWindow;
        } else if (WindowReader::effectively_visible(window) && !cloaked(window)) {
            id = tracker_.id_of(window);
        }
        if (!id.has_value()) continue;
        stacking.order.windows.push_back(*id);
        if (stacking.top == nullptr) stacking.top = window;
        if (stacking.top_ordinary == nullptr && (::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0) {
            stacking.top_ordinary = window;
        }
    }
    HWND active = active_window();
    stacking.active = active;
    if (active != nullptr && active != marker) {
        stacking.order.active = tracker_.id_of(active).value_or(WindowId{});
    }
    return stacking;
}

// Ready is sent at once, whenever it is looked at. Not ready, the wait
// goes on, and each time it runs out it is longer, until the last.
void ZOrderReport::look(Clock::time_point now) {
    const Stacking stacking = current();
    void* wanted = expected_ != nullptr ? expected_ : stacking.active;
    const bool ready = wanted == nullptr || wanted == stacking.top || wanted == stacking.top_ordinary
                       || (expected_ == nullptr && stacking.order.active.none());
    if (!ready) {
        if (due_.has_value() && now < *due_) return;
        if (wait_ms_ < kLastWaitMs) {
            wait_ms_ = wait_ms_ == 0 ? kFirstWaitMs : wait_ms_ * 2;
            due_ = now + std::chrono::milliseconds(wait_ms_);
            return;
        }
        RWIN_DEBUG("zorder", "{:#x} is still not on top; the stacking is said as it stands",
                   reinterpret_cast<std::uintptr_t>(wanted));
    }
    expected_ = nullptr;
    wait_ms_ = 0;
    due_.reset();
    send(stacking.order);
}

void ZOrderReport::send(const ZOrder& order) {
    if (sent_ == order) return;
    MessageWriter out{MessageId::ZOrder};
    order.write(out);
    outbox_.send(out.take());
    RWIN_DEBUG("zorder", "{} window(s), active {}", order.windows.size(), order.active);
    sent_ = order;
}

}  // namespace rwin::guest
