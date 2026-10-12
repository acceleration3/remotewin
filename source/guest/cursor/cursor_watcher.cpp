#include "guest/cursor/cursor_watcher.hpp"

#include "guest/capture/frame_signal.hpp"
#include "logging/logger.hpp"

// clang-format off
#include <windows.h>
// clang-format on

namespace rwin::guest {

namespace {

// The hook is a plain function with no argument of its own, and the
// watcher owns the only thread it runs on. One watcher exists per
// session, which is what makes a single pointer enough.
std::atomic<CursorWatcher*> listening{nullptr};

void CALLBACK on_cursor_event(HWINEVENTHOOK /*hook*/, DWORD /*event*/, HWND /*window*/, LONG object,
                              LONG /*child*/, DWORD /*thread*/, DWORD /*time*/) {
    if (object != OBJID_CURSOR) return;
    if (const CursorWatcher* watcher = listening.load(); watcher != nullptr) watcher->heard();
}

}  // namespace

// Waits for the thread to have its message queue, so the quit the
// destructor posts cannot come before there is a queue to post it to.
CursorWatcher::CursorWatcher(const FrameSignal& changes) : changes_(changes) {
    worker_ = std::thread([this] { listen(); });
    while (thread_id_.load() == 0) std::this_thread::yield();
}

CursorWatcher::~CursorWatcher() {
    ::PostThreadMessageW(thread_id_.load(), WM_QUIT, 0, 0);
    worker_.join();
}

void CursorWatcher::heard() const noexcept { changes_.raise(); }

void CursorWatcher::listen() {
    // Peeking makes the thread's message queue.
    MSG message;
    ::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    listening.store(this);
    thread_id_.store(::GetCurrentThreadId());
    // Out of context so the events arrive on this thread's queue rather
    // than by loading this code into every process that has a window.
    HWINEVENTHOOK shape = ::SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr,
                                            &on_cursor_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    HWINEVENTHOOK shown = ::SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, &on_cursor_event, 0, 0,
                                            WINEVENT_OUTOFCONTEXT);
    if (shape == nullptr || shown == nullptr) {
        RWIN_WARN("cursor", "no cursor hook; the cursor is looked at once a frame");
    }
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    if (shape != nullptr) ::UnhookWinEvent(shape);
    if (shown != nullptr) ::UnhookWinEvent(shown);
    listening.store(nullptr);
}

std::optional<GuestCursor> CursorWatcher::changed() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (::GetCursorInfo(&info) == FALSE) return std::nullopt;

    const bool shown = (info.flags & CURSOR_SHOWING) != 0 && (info.flags & CURSOR_SUPPRESSED) == 0
                       && info.hCursor != nullptr;
    // A hidden cursor's handle is whichever was last set, and says nothing
    // the host shows.
    const GuestCursor now{shown ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info.hCursor)) : 0,
                          shown};
    if (last_ == now) return std::nullopt;
    last_ = now;
    return now;
}

}  // namespace rwin::guest
