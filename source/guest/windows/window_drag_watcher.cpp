#include "guest/windows/window_drag_watcher.hpp"

#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

// The hook is a plain function with no argument of its own, and the
// watcher owns the only thread it runs on. One watcher exists per
// session, which is what makes a single pointer enough.
std::atomic<WindowDragWatcher*> listening{nullptr};

// A queue this long means nothing is draining it; the oldest drags are
// the least interesting.
constexpr std::size_t kMaxPending = 256;

void CALLBACK on_move_size_start(HWINEVENTHOOK /*hook*/, DWORD event, HWND window,
                                 LONG /*object*/, LONG /*child*/, DWORD /*thread*/,
                                 DWORD /*time*/) {
    if (event != EVENT_SYSTEM_MOVESIZESTART || window == nullptr) return;
    WindowDragWatcher* watcher = listening.load();
    if (watcher == nullptr) return;

    // An owned window is mirrored as a popup of its owner, placed where
    // the guest put it — the one surface the compositor lets the client
    // position, and the one it will not drag. So Windows keeps this drag:
    // the guest window moves, and the mirror follows the positions the
    // window list reports.
    HWND owner = ::GetWindow(window, GW_OWNER);
    if (owner != nullptr && ::IsWindowVisible(owner) != FALSE) return;

    // Break the move loop before it moves anything. WM_CANCELMODE is what
    // ends any modal tracking state, and the window's own procedure acts
    // on it at its next message — microseconds after it began, so the
    // guest window never visibly moves.
    ::PostMessageW(window, WM_CANCELMODE, 0, 0);
    watcher->note(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window)));
}

}  // namespace

WindowDragWatcher::WindowDragWatcher() {
    worker_ = std::thread([this] { listen(); });
}

WindowDragWatcher::~WindowDragWatcher() {
    if (const unsigned long id = thread_id_.load(); id != 0) {
        ::PostThreadMessageW(id, WM_QUIT, 0, 0);
    }
    if (worker_.joinable()) worker_.join();
}

void WindowDragWatcher::listen() {
    listening.store(this);
    thread_id_.store(::GetCurrentThreadId());

    // Out of context so the events arrive on this thread's queue rather
    // than by loading this code into every process that has a window.
    HWINEVENTHOOK hook = ::SetWinEventHook(
        EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART, nullptr,
        &on_move_size_start, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (hook == nullptr) {
        RWIN_WARN("drag", "no move hook; dragging a window will move it in the guest");
        listening.store(nullptr);
        thread_id_.store(0);
        return;
    }
    RWIN_INFO("drag", "watching for drags");

    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }

    ::UnhookWinEvent(hook);
    listening.store(nullptr);
    thread_id_.store(0);
}

void WindowDragWatcher::note(std::uint64_t window_handle) {
    const std::lock_guard lock{mutex_};
    if (started_.size() >= kMaxPending) started_.erase(started_.begin());
    started_.push_back(window_handle);
    RWIN_DEBUG("drag", "{:#x} started a drag; cancelled and handed to the host",
               window_handle);
}

std::vector<std::uint64_t> WindowDragWatcher::drained() {
    const std::lock_guard lock{mutex_};
    std::vector<std::uint64_t> out;
    out.swap(started_);
    return out;
}

}  // namespace rwin::guest
