#include "guest/windows/move_size_report.hpp"

#include "guest/session/outbox.hpp"
#include "guest/windows/min_max_reader.hpp"
#include "guest/windows/window_surface.hpp"
#include "guest/windows/window_tracker.hpp"
#include "logging/logger.hpp"
#include "proto/message_writer.hpp"
#include "proto/move_size_end.hpp"
#include "proto/move_size_start.hpp"

// clang-format off
#include <windows.h>
// clang-format on

#include <array>

namespace rwin::guest {

namespace {

constexpr const wchar_t* kIgnoreList =
    L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Terminal Server\\Compatibility\\LocalMoveSizeIgnoreList";
constexpr DWORD kLongestValueName = 16384;
constexpr long kKeyboardSize = 0;
constexpr long kLastEdge = 8;
constexpr long kMove = 9;
constexpr long kKeyboardMove = 10;

std::set<std::wstring> read_ignore_list() {
    std::set<std::wstring> names;
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kIgnoreList, 0, KEY_READ, &key) != ERROR_SUCCESS) return names;
    std::wstring name(kLongestValueName, L'\0');
    for (DWORD index = 0;; ++index) {
        DWORD length = kLongestValueName;
        if (::RegEnumValueW(key, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) {
            break;
        }
        names.emplace(name.data(), length);
    }
    ::RegCloseKey(key);
    return names;
}

std::optional<MoveSizeKind> kind_of(long child) {
    if (child == kKeyboardSize) return MoveSizeKind::KeySize;
    if (child >= 1 && child <= kLastEdge) return static_cast<MoveSizeKind>(child);
    if (child == kMove) return MoveSizeKind::Move;
    if (child == kKeyboardMove) return MoveSizeKind::KeyMove;
    return std::nullopt;
}

Rect frame_of(void* window) {
    const WindowSurface surface{static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(window))};
    return Rect{surface.screen_x(0), surface.screen_y(0), surface.width(), surface.height()};
}

}  // namespace

MoveSizeReport::MoveSizeReport(WindowTracker& tracker, Outbox& outbox)
    : tracker_(tracker), outbox_(outbox), ignored_(read_ignore_list()) {
    if (!ignored_.empty()) RWIN_INFO("movesize", "{} process(es) keep Windows' own move loop", ignored_.size());
}

bool MoveSizeReport::ignored(void* window) const {
    if (ignored_.empty()) return false;
    DWORD process_id = 0;
    ::GetWindowThreadProcessId(static_cast<HWND>(window), &process_id);
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
    if (process == nullptr) return false;
    std::array<wchar_t, MAX_PATH * 2> path{};
    DWORD length = static_cast<DWORD>(path.size());
    const bool named = ::QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE;
    ::CloseHandle(process);
    if (!named) return false;
    const std::wstring_view image(path.data(), length);
    const std::size_t slash = image.find_last_of(L'\\');
    return ignored_.contains(std::wstring(slash == std::wstring_view::npos ? image : image.substr(slash + 1)));
}

void MoveSizeReport::started(void* window, long child) {
    const std::optional<MoveSizeKind> kind = kind_of(child);
    const std::optional<WindowId> id = tracker_.admitted(window);
    if (!kind.has_value() || !id.has_value() || ignored(window)) return;
    moving_.insert_or_assign(window, *kind);
    MessageWriter limits{MessageId::MinMax};
    read_min_max(window, *id).write(limits);
    outbox_.send(limits.take());
    POINT cursor{};
    ::GetCursorPos(&cursor);
    MoveSizeStart start{*id, *kind, cursor.x, cursor.y};
    if (*kind == MoveSizeKind::Move) {
        const Rect frame = frame_of(window);
        start.x -= frame.x;
        start.y -= frame.y;
    }
    MessageWriter out{MessageId::MoveSizeStart};
    start.write(out);
    outbox_.send(out.take());
    RWIN_DEBUG("movesize", "{} started: kind {} at {},{}", *id, static_cast<int>(*kind), start.x, start.y);
}

void MoveSizeReport::ended(void* window) {
    const auto found = moving_.find(window);
    if (found == moving_.end()) return;
    const MoveSizeKind kind = found->second;
    moving_.erase(found);
    const std::optional<WindowId> id = tracker_.id_of(window);
    if (!id.has_value()) return;
    MessageWriter out{MessageId::MoveSizeEnd};
    MoveSizeEnd{*id, kind, frame_of(window)}.write(out);
    outbox_.send(out.take());
    tracker_.refresh(window);
    RWIN_DEBUG("movesize", "{} ended", *id);
}

}  // namespace rwin::guest
