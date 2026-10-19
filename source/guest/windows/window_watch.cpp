#include "guest/windows/window_watch.hpp"

#include "guest/session/outbox.hpp"
#include "guest/windows/app_id_resolver.hpp"
#include "guest/windows/move_size_report.hpp"
#include "guest/windows/shell_views.hpp"
#include "guest/windows/window_commands.hpp"
#include "guest/windows/window_directory.hpp"
#include "guest/windows/window_reader.hpp"
#include "guest/windows/window_surface.hpp"
#include "guest/windows/window_tracker.hpp"
#include "guest/windows/z_order_report.hpp"
#include "logging/logger.hpp"
#include "proto/message_writer.hpp"
#include "proto/sys_param.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
// clang-format on

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>

namespace rwin::guest {

namespace {

using Clock = std::chrono::steady_clock;

// rdpshell's table of WinEvent ranges (CHook::CommandStart): foreground;
// destroy, show and hide; focus; state, location and name; move/size end;
// the undocumented move/size start that names its kind; parent change;
// cloak and uncloak.
constexpr DWORD kMoveSizeKind = 0x0019;
constexpr std::array<std::pair<DWORD, DWORD>, 8> kHookedEvents{{
    {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND},
    {EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE},
    {EVENT_OBJECT_FOCUS, EVENT_OBJECT_FOCUS},
    {EVENT_OBJECT_STATECHANGE, EVENT_OBJECT_NAMECHANGE},
    {EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND},
    {kMoveSizeKind, kMoveSizeKind},
    {EVENT_OBJECT_PARENTCHANGE, EVENT_OBJECT_PARENTCHANGE},
    {EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED},
}};

// Windows destroyed without their event reaching here are looked for this
// often; a hook's events are posted messages, and a full queue drops them.
constexpr std::chrono::milliseconds kSweepEvery{1000};
// DWM updates a window's extended frame bounds after the move that raised
// the location event, so a frame read at the event can be the old one; a
// moved window is read again this much later.
constexpr std::chrono::milliseconds kFrameSettles{60};

// The hook is a plain function with no argument of its own, and runs on
// the thread that set it.
thread_local WindowWatch* listening = nullptr;

void CALLBACK on_event(HWINEVENTHOOK /*hook*/, DWORD event, HWND window, LONG object, LONG child,
                       DWORD /*thread*/, DWORD /*time*/) {
    if (object != OBJID_WINDOW || window == nullptr || listening == nullptr) return;
    // A destroyed window answers no question about itself.
    if (event != EVENT_OBJECT_DESTROY && !WindowReader::top_level(window)) return;
    listening->heard(event, window, child);
}

class Hooks {
public:
    Hooks() {
        for (const auto& [first, last] : kHookedEvents) {
            HWINEVENTHOOK hook = ::SetWinEventHook(first, last, nullptr, &on_event, 0, 0,
                                                   WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (hook == nullptr) {
                RWIN_ERROR("windows", "no hook for events {:#x}-{:#x} (error {}): windows will go unnoticed", first,
                           last, ::GetLastError());
                continue;
            }
            hooks_.push_back(hook);
        }
    }
    ~Hooks() {
        for (HWINEVENTHOOK hook : hooks_) ::UnhookWinEvent(hook);
    }
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;
    Hooks(Hooks&&) = delete;
    Hooks& operator=(Hooks&&) = delete;

private:
    std::vector<HWINEVENTHOOK> hooks_;
};

HWND root_owner_of(HWND window) {
    HWND root = ::GetAncestor(window, GA_ROOTOWNER);
    return root != nullptr ? root : window;
}

}  // namespace

struct WindowWatch::Parts {
    explicit Parts(WindowWatch& watch)
        : reader(watch.directory_, app_ids, shell, watch.settings_),
          tracker(watch.directory_, reader, watch.outbox_, watch.features_),
          zorder(tracker, watch.marker_, watch.outbox_),
          movesize(tracker, watch.outbox_) {}

    AppIdResolver app_ids;
    ShellViews shell;
    WindowReader reader;
    WindowTracker tracker;
    ZOrderReport zorder;
    MoveSizeReport movesize;
    std::vector<Event> serving;
    // Moved windows to read again, and when.
    std::vector<std::pair<void*, Clock::time_point>> settling;
    Clock::time_point next_sweep = Clock::now() + kSweepEvery;
    bool restack = false;
};

WindowWatch::WindowWatch(WindowDirectory& directory, Outbox& outbox, FeatureSet features)
    : directory_(directory), outbox_(outbox), features_(std::move(features)) {
    worker_ = std::thread([this] { run(); });
}

WindowWatch::~WindowWatch() {
    running_.store(false);
    wake_.raise();
    if (worker_.joinable()) worker_.join();
}

void WindowWatch::ask(const Activate& request) { queue(request); }
void WindowWatch::ask(const SysCommand& request) { queue(request); }
void WindowWatch::ask(const SysMenu& request) { queue(request); }
void WindowWatch::ask(const WindowMove& request) { queue(request); }
void WindowWatch::reread(WindowId window) { queue(window); }
void WindowWatch::dismiss(WindowId popup) { queue(Dismissal{popup}); }

void WindowWatch::queue(Request request) {
    {
        const std::lock_guard held{requests_lock_};
        requests_.push_back(std::move(request));
    }
    wake_.raise();
}

void WindowWatch::heard(unsigned long event, void* window, long child) {
    heard_.push_back(Event{event, window, child});
}

void WindowWatch::run() {
    const HRESULT apartment = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    MSG message;
    ::PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    listening = this;
    {
        // Hooked before the first look, so nothing shown in between is missed.
        const Hooks hooks;
        Parts parts{*this};
        say_work_area();
        parts.tracker.sync(SyncBegin::Reason::Connect);
        parts.zorder.look(Clock::now());
        serve(parts);
    }
    listening = nullptr;
    if (SUCCEEDED(apartment)) ::CoUninitialize();
}

void WindowWatch::serve(Parts& parts) {
    const auto wake = static_cast<HANDLE>(wake_.event());
    while (running_.load()) {
        const Clock::time_point now = Clock::now();
        Clock::time_point until = parts.next_sweep;
        if (const auto due = parts.zorder.due()) until = std::min(until, *due);
        for (const auto& [window, due] : parts.settling) until = std::min(until, due);
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(until - now).count();
        ::MsgWaitForMultipleObjectsEx(1, &wake, static_cast<DWORD>(std::max<long long>(wait, 0)), QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
        pump();
        parts.serving.clear();
        parts.serving.swap(heard_);
        for (std::size_t i = 0; i < parts.serving.size(); ++i) {
            if (i > 0 && parts.serving[i] == parts.serving[i - 1]) continue;
            take(parts, parts.serving[i]);
        }
        std::vector<Request> asked;
        {
            const std::lock_guard held{requests_lock_};
            asked.swap(requests_);
        }
        for (const Request& request : asked) carry_out(parts, request);
        settle(parts);
        if (settings_.changed()) {
            say_work_area();
            parts.tracker.refresh_all();
        }
        if (Clock::now() >= parts.next_sweep) {
            parts.tracker.sweep();
            parts.next_sweep = Clock::now() + kSweepEvery;
            parts.restack = true;
        }
        const auto due = parts.zorder.due();
        if (parts.restack || (due.has_value() && Clock::now() >= *due)) parts.zorder.look(Clock::now());
        parts.restack = false;
    }
}

// The hook's events arrive as this thread's messages and are queued by the
// hook as they are dispatched.
void WindowWatch::pump() {
    MSG message;
    while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE) {
        if (message.message == WM_QUIT) {
            running_.store(false);
            return;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
}

void WindowWatch::take(Parts& parts, const Event& event) {
    using Change = WindowTracker::Change;
    switch (event.event) {
        case EVENT_SYSTEM_FOREGROUND:
        case EVENT_OBJECT_FOCUS:
            parts.tracker.changed(event.window, Change::Focused);
            break;
        case EVENT_OBJECT_DESTROY:
            parts.tracker.changed(event.window, Change::Destroyed);
            break;
        case EVENT_OBJECT_SHOW:
            parts.tracker.changed(event.window, Change::Shown);
            break;
        case EVENT_OBJECT_HIDE:
            parts.tracker.changed(event.window, Change::Hidden);
            break;
        case EVENT_OBJECT_LOCATIONCHANGE:
            parts.tracker.changed(event.window, Change::Moved);
            if (std::none_of(parts.settling.begin(), parts.settling.end(),
                             [&](const auto& entry) { return entry.first == event.window; })) {
                parts.settling.emplace_back(event.window, Clock::now() + kFrameSettles);
            }
            return;
        case EVENT_OBJECT_STATECHANGE:
        case EVENT_OBJECT_NAMECHANGE:
        case EVENT_OBJECT_CLOAKED:
        case EVENT_OBJECT_UNCLOAKED:
            parts.tracker.changed(event.window, Change::Restyled);
            break;
        case EVENT_OBJECT_PARENTCHANGE:
            parts.tracker.changed(event.window, Change::Reparented);
            break;
        case kMoveSizeKind:
            if (features_.has(Feature::kMoveSize)) parts.movesize.started(event.window, event.child);
            return;
        case EVENT_SYSTEM_MOVESIZEEND:
            if (features_.has(Feature::kMoveSize)) parts.movesize.ended(event.window);
            return;
        default:
            return;
    }
    parts.restack = true;
}

void WindowWatch::settle(Parts& parts) {
    const Clock::time_point now = Clock::now();
    for (auto entry = parts.settling.begin(); entry != parts.settling.end();) {
        if (now < entry->second) {
            ++entry;
            continue;
        }
        parts.tracker.refresh(entry->first);
        entry = parts.settling.erase(entry);
    }
}

void WindowWatch::carry_out(Parts& parts, const Request& request) {
    if (const auto* activation = std::get_if<Activate>(&request)) {
        activate(parts, *activation);
        return;
    }
    if (const auto* reread = std::get_if<WindowId>(&request)) {
        if (void* window = directory_.handle_of(*reread)) parts.tracker.refresh(window);
        return;
    }
    if (const auto* command = std::get_if<SysCommand>(&request)) {
        if (void* window = directory_.handle_of(command->window)) post_sys_command(window, command->command);
        return;
    }
    if (const auto* menu = std::get_if<SysMenu>(&request)) {
        void* window = directory_.handle_of(menu->window);
        if (window == nullptr) return;
        const WindowSurface surface{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window))};
        post_system_menu(window, surface.screen_x(menu->x), surface.screen_y(menu->y));
        return;
    }
    if (const auto* move = std::get_if<WindowMove>(&request)) {
        if (void* window = directory_.handle_of(move->window)) place(window, move->frame, settings_.work_area());
        return;
    }
    if (const auto* dismissal = std::get_if<Dismissal>(&request)) {
        if (void* window = directory_.handle_of(dismissal->popup)) dismiss_popup(window);
    }
}

// Not enabled names the window the host's focus left: the marker takes the
// foreground only while that window still has it, so a deactivation that
// trails a press in another mirror does not undo the press.
void WindowWatch::activate(Parts& parts, const Activate& request) {
    const auto window = static_cast<HWND>(directory_.handle_of(request.window));
    if (window == nullptr) {
        RWIN_DEBUG("focus", "activation of {}, which is not a window here", request.window);
        return;
    }
    if (request.enabled) {
        parts.zorder.expect(window);
        marker_.activate(window);
        RWIN_DEBUG("focus", "{} activated by the host", request.window);
        return;
    }
    HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr || root_owner_of(foreground) != root_owner_of(window)) {
        RWIN_DEBUG("focus", "deactivation of {} is stale: the foreground moved on", request.window);
        return;
    }
    parts.zorder.expect(marker_.handle());
    marker_.activate(marker_.handle());
    RWIN_DEBUG("focus", "{} deactivated: the marker has the foreground", request.window);
}

void WindowWatch::say_work_area() {
    if (!features_.has(Feature::kWindows)) return;
    MessageWriter out{MessageId::GuestSysParam};
    SysParam{SysParam::Param::WorkArea, settings_.work_area(), false}.write(out);
    outbox_.send(out.take());
    MessageWriter monitor{MessageId::GuestSysParam};
    SysParam{SysParam::Param::Monitor, settings_.monitor(), false}.write(monitor);
    outbox_.send(monitor.take());
}

}  // namespace rwin::guest
